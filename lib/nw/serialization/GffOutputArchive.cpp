#include "GffOutputArchive.hpp"

#include "../log.hpp"

#include <filesystem>
#include <fstream>

namespace nw {

// -- GffOutputArchiveStruct --------------------------------------------------

GffOutputArchiveStruct::GffOutputArchiveStruct(GffOutputArchive* parent_)
    : parent{parent_}
{
}

GffOutputArchiveList& GffOutputArchiveStruct::add_list(std::string_view name)
{
    GffOutputArchiveField f{parent};
    f.label_index = static_cast<uint32_t>(parent->add_label(name));
    f.type = SerializationType::LIST;
    f.structures = GffOutputArchiveList{parent};
    field_entries.push_back(f);
    return std::get<GffOutputArchiveList>(field_entries.back().structures);
}

GffOutputArchiveStruct& GffOutputArchiveStruct::add_struct(std::string_view name, uint32_t id)
{

    field_entries.emplace_back(parent);
    GffOutputArchiveField& f = field_entries.back();
    f.label_index = static_cast<uint32_t>(parent->add_label(name));
    f.type = SerializationType::STRUCT;
    f.structures = GffOutputArchiveStruct{parent};
    return std::get<GffOutputArchiveStruct>(field_entries.back().structures);
}

// -- GffOutputArchiveList ----------------------------------------------------

GffOutputArchiveList::GffOutputArchiveList(GffOutputArchive* parent_)
    : parent{parent_}
{
}

GffOutputArchiveStruct& GffOutputArchiveList::push_back(uint32_t id)
{
    structs.emplace_back(parent);
    GffOutputArchiveStruct& result = structs.back();
    result.id = id;
    return result;
}

// -- GffOutputArchiveField ---------------------------------------------------

GffOutputArchiveField::GffOutputArchiveField(GffOutputArchive* parent_)
    : parent{parent_}
{
}

// -- GffOutputArchive --------------------------------------------------------

GffOutputArchive::GffOutputArchive(std::string_view type, std::string_view version)
    : top{this}
{
    std::memcpy(header.type, type.data(), 3);
    header.type[3] = ' ';
    std::memcpy(header.version, version.data(), 4);

    top.id = 0xffffffff;
}

size_t GffOutputArchive::add_label(std::string_view name)
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

void build_arrays(GffOutputArchive& archive, GffOutputArchiveField& field);
void build_arrays(GffOutputArchive& archive, GffOutputArchiveStruct& str);
void build_indicies(GffOutputArchive& archive, const GffOutputArchiveField& field);
void build_indicies(GffOutputArchive& archive, const GffOutputArchiveStruct& str);

void build_arrays(GffOutputArchive& archive, GffOutputArchiveField& field)
{
    field.index = archive.field_entries.size();
    archive.field_entries.push_back({field.type, field.label_index, field.data_or_offset});
    if (field.type == SerializationType::STRUCT) {
        build_arrays(archive, std::get<GffOutputArchiveStruct>(field.structures));
    } else if (field.type == SerializationType::LIST) {
        auto& list = std::get<GffOutputArchiveList>(field.structures);
        for (auto& s : list.structs) {
            build_arrays(archive, s);
        }
    }
}

void build_arrays(GffOutputArchive& archive, GffOutputArchiveStruct& str)
{
    str.index = archive.struct_entries.size();
    archive.struct_entries.push_back({str.id, 0, static_cast<uint32_t>(str.field_entries.size())});
    for (auto& f : str.field_entries) {
        build_arrays(archive, f);
    }
}

void build_indicies(GffOutputArchive& archive, const GffOutputArchiveField& field)
{
    if (field.type == SerializationType::STRUCT) {
        const auto& data = std::get<GffOutputArchiveStruct>(field.structures);
        archive.field_entries[field.index].data_or_offset = data.index;
        build_indicies(archive, data);
    } else if (field.type == SerializationType::LIST) {
        const auto& data = std::get<GffOutputArchiveList>(field.structures);
        archive.field_entries[field.index].data_or_offset = archive.list_indices.size() * 4; // byte offset
        archive.list_indices.push_back(data.structs.size());
        for (auto& s : data.structs) {
            archive.list_indices.push_back(s.index);
        }
        for (auto& s : data.structs) {
            build_indicies(archive, s);
        }
    }
}

void build_indicies(GffOutputArchive& archive, const GffOutputArchiveStruct& str)
{
    if (str.field_entries.size() == 1) {
        archive.struct_entries[str.index].field_index = str.field_entries[0].index;
        build_indicies(archive, str.field_entries[0]);
    } else {
        archive.struct_entries[str.index].field_index = archive.field_indices.size() * 4; // byte offset
        for (auto& f : str.field_entries) {
            archive.field_indices.push_back(f.index);
        }
        for (auto& f : str.field_entries) {
            build_indicies(archive, f);
        }
    }
}

void GffOutputArchive::build()
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

ByteArray GffOutputArchive::to_byte_array() const
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

bool GffOutputArchive::write_to(const fs::path& filename) const
{
    fs::path temp = fs::temp_directory_path() / filename.filename();

    std::ofstream out(temp, std::ios_base::binary);
    if (!out.good())
        return false;

    out.write((const char*)&header, sizeof(header));
    out.write((const char*)struct_entries.data(), struct_entries.size() * 12);
    out.write((const char*)field_entries.data(), field_entries.size() * 12);
    out.write((const char*)labels.data(), labels.size() * 16);
    out.write((const char*)data.data(), data.size());
    out.write((const char*)field_indices.data(), field_indices.size() * 4);
    out.write((const char*)list_indices.data(), list_indices.size() * 4);
    out.close();

    fs::copy_file(temp, filename, fs::copy_options::overwrite_existing);
    fs::remove(temp);

    return true;
}

} // namespace nw
