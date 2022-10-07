#include "GffBuilder.hpp"

#include "../log.hpp"
#include "../util/templates.hpp"

#include <filesystem>
#include <fstream>

namespace nw {

// -- GffBuilderStruct --------------------------------------------------

GffBuilderStruct::GffBuilderStruct(GffBuilder* parent_)
    : parent{parent_}
{
}

GffBuilderList& GffBuilderStruct::add_list(std::string_view name)
{
    GffBuilderField f{parent};
    f.label_index = static_cast<uint32_t>(parent->add_label(name));
    f.type = SerializationType::list;
    f.structures = GffBuilderList{parent};
    field_entries.push_back(f);
    return std::get<GffBuilderList>(field_entries.back().structures);
}

GffBuilderStruct& GffBuilderStruct::add_struct(std::string_view name, uint32_t id_)
{

    field_entries.emplace_back(parent);
    GffBuilderField& f = field_entries.back();
    f.label_index = static_cast<uint32_t>(parent->add_label(name));
    f.type = SerializationType::struct_;
    f.structures = GffBuilderStruct{parent};
    auto& result = std::get<GffBuilderStruct>(field_entries.back().structures);
    result.id = id_;
    return result;
}

// -- GffBuilderList ----------------------------------------------------

GffBuilderList::GffBuilderList(GffBuilder* parent_)
    : parent{parent_}
{
}

GffBuilderStruct& GffBuilderList::push_back(uint32_t id)
{
    structs.emplace_back(parent);
    GffBuilderStruct& result = structs.back();
    result.id = id;
    return result;
}

// -- GffBuilderField ---------------------------------------------------

GffBuilderField::GffBuilderField(GffBuilder* parent_)
    : parent{parent_}
{
}

// -- GffBuilder --------------------------------------------------------

GffBuilder::GffBuilder(std::string_view type, std::string_view version)
    : top{this}
{
    std::memcpy(header.type, type.data(), 3);
    header.type[3] = ' ';
    std::memcpy(header.version, version.data(), 4);

    top.id = 0xffffffff;
}

size_t GffBuilder::add_label(std::string_view name)
{
    auto it = std::find_if(std::begin(labels), std::end(labels),
        [&name](const GffLabel& label) -> bool {
            return label.view() == name;
        });
    if (it == std::end(labels)) {
        labels.emplace_back(name);
        return labels.size() - 1;
    } else {
        return static_cast<size_t>(std::distance(std::begin(labels), it));
    }
}

void build_arrays(GffBuilder& archive, GffBuilderField& field);
void build_arrays(GffBuilder& archive, GffBuilderStruct& str);
void build_indicies(GffBuilder& archive, const GffBuilderField& field);
void build_indicies(GffBuilder& archive, const GffBuilderStruct& str);

void build_arrays(GffBuilder& archive, GffBuilderField& field)
{
    field.index = static_cast<uint32_t>(archive.field_entries.size());
    archive.field_entries.push_back({field.type, field.label_index, field.data_or_offset});
    if (field.type == SerializationType::struct_) {
        build_arrays(archive, std::get<GffBuilderStruct>(field.structures));
    } else if (field.type == SerializationType::list) {
        auto& list = std::get<GffBuilderList>(field.structures);
        for (auto& s : list.structs) {
            build_arrays(archive, s);
        }
    }
}

void build_arrays(GffBuilder& archive, GffBuilderStruct& str)
{
    str.index = static_cast<uint32_t>(archive.struct_entries.size());
    archive.struct_entries.push_back({str.id, 0, static_cast<uint32_t>(str.field_entries.size())});
    for (auto& f : str.field_entries) {
        build_arrays(archive, f);
    }
}

void build_indicies(GffBuilder& archive, const GffBuilderField& field)
{
    if (field.type == SerializationType::struct_) {
        const auto& data = std::get<GffBuilderStruct>(field.structures);
        archive.field_entries[field.index].data_or_offset = data.index;
        build_indicies(archive, data);
    } else if (field.type == SerializationType::list) {
        const auto& data = std::get<GffBuilderList>(field.structures);
        archive.field_entries[field.index].data_or_offset = static_cast<uint32_t>(archive.list_indices.size() * 4); // byte offset
        archive.list_indices.push_back(static_cast<uint32_t>(data.structs.size()));
        for (auto& s : data.structs) {
            archive.list_indices.push_back(s.index);
        }
        for (auto& s : data.structs) {
            build_indicies(archive, s);
        }
    }
}

void build_indicies(GffBuilder& archive, const GffBuilderStruct& str)
{
    if (str.field_entries.size() == 1) {
        archive.struct_entries[str.index].field_index = str.field_entries[0].index;
        build_indicies(archive, str.field_entries[0]);
    } else {
        archive.struct_entries[str.index].field_index = static_cast<uint32_t>(archive.field_indices.size() * 4); // byte offset
        for (auto& f : str.field_entries) {
            archive.field_indices.push_back(f.index);
        }
        for (auto& f : str.field_entries) {
            build_indicies(archive, f);
        }
    }
}

void GffBuilder::build()
{
    build_arrays(*this, top);
    build_indicies(*this, top);

    header.struct_offset = sizeof(GffHeader);
    header.struct_count = static_cast<uint32_t>(struct_entries.size());
    header.field_offset = static_cast<uint32_t>(header.struct_offset + struct_entries.size() * 12);
    header.field_count = static_cast<uint32_t>(field_entries.size());
    header.label_offset = static_cast<uint32_t>(header.field_offset + field_entries.size() * 12);
    header.label_count = static_cast<uint32_t>(labels.size());
    header.field_data_offset = static_cast<uint32_t>(header.label_offset + labels.size() * 16);
    header.field_data_count = static_cast<uint32_t>(data.size());
    header.field_idx_offset = static_cast<uint32_t>(header.field_data_offset + data.size());
    header.field_idx_count = static_cast<uint32_t>(field_indices.size() * 4);
    header.list_idx_offset = static_cast<uint32_t>(header.field_idx_offset + field_indices.size() * 4);
    header.list_idx_count = static_cast<uint32_t>(list_indices.size() * 4);
}

ByteArray GffBuilder::to_byte_array() const
{
    ByteArray result;
    result.append(&header, sizeof(header));
    result.append(struct_entries.data(), struct_entries.size() * 12);
    result.append(field_entries.data(), field_entries.size() * 12);
    result.append(labels.data(), labels.size() * 16);
    result.append(data.data(), data.size());
    result.append(field_indices.data(), field_indices.size() * 4);
    result.append(list_indices.data(), list_indices.size() * 4);
    return result;
}

namespace fs = std::filesystem;

bool GffBuilder::write_to(const fs::path& filename) const
{
    fs::path temp = fs::temp_directory_path() / filename.filename();

    std::ofstream out(temp, std::ios_base::binary);
    if (!out.good())
        return false;

    ostream_write(out, &header, sizeof(header));
    ostream_write(out, struct_entries.data(), struct_entries.size() * 12);
    ostream_write(out, field_entries.data(), field_entries.size() * 12);
    ostream_write(out, labels.data(), labels.size() * 16);
    ostream_write(out, data.data(), data.size());
    ostream_write(out, field_indices.data(), field_indices.size() * 4);
    ostream_write(out, list_indices.data(), list_indices.size() * 4);
    out.close();

    fs::copy_file(temp, filename, fs::copy_options::overwrite_existing);
    fs::remove(temp);

    return true;
}

} // namespace nw
