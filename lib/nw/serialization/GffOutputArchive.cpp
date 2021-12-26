#include "GffOutputArchive.hpp"

#include <filesystem>
#include <fstream>

namespace nw {

// -- GffOutputArchiveStruct --------------------------------------------------

GffOutputArchiveStruct::GffOutputArchiveStruct(GffOutputArchive* parent_)
    : parent{parent_}
{
}

void GffOutputArchiveStruct::add_field(std::string_view name, serialization_cref value)
{
    GffOutputArchiveField f{parent};
    f.label_index = parent->add_label(name);

    if (std::holds_alternative<std::reference_wrapper<const bool>>(value)) {
        f.type = SerializationType::id<uint8_t>();
        uint8_t temp = std::get<std::reference_wrapper<const bool>>(value);
        std::memcpy(&f.data_or_offset, &temp, 1);
    } else if (std::holds_alternative<std::reference_wrapper<const uint8_t>>(value)) {
        f.type = SerializationType::id<uint8_t>();
        uint8_t temp = std::get<std::reference_wrapper<const uint8_t>>(value);
        std::memcpy(&f.data_or_offset, &temp, 1);
    } else if (std::holds_alternative<std::reference_wrapper<const int8_t>>(value)) {
        f.type = SerializationType::id<int8_t>();
        int8_t temp = std::get<std::reference_wrapper<const int8_t>>(value);
        std::memcpy(&f.data_or_offset, &temp, 1);
    } else if (std::holds_alternative<std::reference_wrapper<const uint16_t>>(value)) {
        f.type = SerializationType::id<uint16_t>();
        uint16_t temp = std::get<std::reference_wrapper<const uint16_t>>(value);
        std::memcpy(&f.data_or_offset, &temp, 2);
    } else if (std::holds_alternative<std::reference_wrapper<const int16_t>>(value)) {
        f.type = SerializationType::id<int16_t>();
        int16_t temp = std::get<std::reference_wrapper<const int16_t>>(value);
        std::memcpy(&f.data_or_offset, &temp, 2);
    } else if (std::holds_alternative<std::reference_wrapper<const uint32_t>>(value)) {
        f.type = SerializationType::id<uint32_t>();
        uint32_t temp = std::get<std::reference_wrapper<const uint32_t>>(value);
        std::memcpy(&f.data_or_offset, &temp, 4);
    } else if (std::holds_alternative<std::reference_wrapper<const int32_t>>(value)) {
        f.type = SerializationType::id<int32_t>();
        int32_t temp = std::get<std::reference_wrapper<const int32_t>>(value);
        std::memcpy(&f.data_or_offset, &temp, 4);
    } else if (std::holds_alternative<std::reference_wrapper<const uint64_t>>(value)) {
        f.data_or_offset = parent->data.size();
        f.type = SerializationType::id<uint64_t>();
        uint64_t temp = std::get<std::reference_wrapper<const uint64_t>>(value);
        parent->data.append(&temp, 8);
    } else if (std::holds_alternative<std::reference_wrapper<const int64_t>>(value)) {
        f.data_or_offset = parent->data.size();
        f.type = SerializationType::id<int64_t>();
        int64_t temp = std::get<std::reference_wrapper<const int64_t>>(value);
        parent->data.append(&temp, 8);
    } else if (std::holds_alternative<std::reference_wrapper<const float>>(value)) {
        f.type = SerializationType::id<float>();
        float temp = std::get<std::reference_wrapper<const float>>(value);
        std::memcpy(&f.data_or_offset, &temp, 4);
    } else if (std::holds_alternative<std::reference_wrapper<const double>>(value)) {
        f.data_or_offset = parent->data.size();
        f.type = SerializationType::id<double>();
        double temp = std::get<std::reference_wrapper<const double>>(value);
        parent->data.append(&temp, 8);
    } else if (std::holds_alternative<std::reference_wrapper<const std::string>>(value)) {
        const std::string& temp = std::get<std::reference_wrapper<const std::string>>(value);
        f.type = SerializationType::id<std::string>();
        f.data_or_offset = parent->data.size();
        uint32_t size = temp.size();
        parent->data.append(&size, 4);
        parent->data.append(temp.data(), size);
    } else if (std::holds_alternative<std::reference_wrapper<const Resref>>(value)) {
        const Resref& temp = std::get<std::reference_wrapper<const Resref>>(value);
        f.type = SerializationType::id<Resref>();
        f.data_or_offset = parent->data.size();
        uint8_t size = static_cast<uint8_t>(temp.length());
        parent->data.append(&size, 1);
        parent->data.append(temp.view().data(), size);
    } else if (std::holds_alternative<std::reference_wrapper<const LocString>>(value)) {
        const LocString& temp = std::get<std::reference_wrapper<const LocString>>(value);
        f.type = SerializationType::id<LocString>();
        f.data_or_offset = parent->data.size();
        uint32_t total_size = 8;
        uint32_t strref = temp.strref(), num_strings = temp.size();
        for (const auto& [lang, str] : temp) {
            total_size += 8 + str.size();
        }
        parent->data.append(&total_size, 4);
        parent->data.append(&strref, 4);
        parent->data.append(&num_strings, 4);
        for (const auto& [lang, str] : temp) {
            uint32_t size = str.size();
            parent->data.append(&lang, 4);
            parent->data.append(&size, 4);
            parent->data.append(str.data(), size);
        }
    } else if (std::holds_alternative<std::reference_wrapper<const ByteArray>>(value)) {
        const ByteArray& temp = std::get<std::reference_wrapper<const ByteArray>>(value);
        f.type = SerializationType::id<ByteArray>();
        f.data_or_offset = parent->data.size();
        uint32_t size = temp.size();
        parent->data.append(&size, 4);
        parent->data.append(temp.data(), size);
    }
    field_entries.push_back(f);
}

void GffOutputArchiveStruct::add_fields(std::initializer_list<std::pair<std::string_view, serialization_cref>> fields)
{
    for (const auto& [name, value] : fields) {
        add_field(name, value);
    }
}

GffOutputArchiveList& GffOutputArchiveStruct::add_list(std::string_view name)
{
    GffOutputArchiveField f{parent};
    f.label_index = parent->add_label(name);
    f.type = SerializationType::LIST;
    f.structures = GffOutputArchiveList{parent};
    field_entries.push_back(f);
    return std::get<GffOutputArchiveList>(field_entries.back().structures);
}

GffOutputArchiveStruct& GffOutputArchiveStruct::add_struct(std::string_view name, uint32_t id)
{

    field_entries.emplace_back(parent);
    GffOutputArchiveField& f = field_entries.back();
    f.label_index = parent->add_label(name);
    f.type = SerializationType::STRUCT;
    f.structures = GffOutputArchiveStruct{parent};
    return std::get<GffOutputArchiveStruct>(field_entries.back().structures);
}

// -- GffOutputArchiveList ----------------------------------------------------

GffOutputArchiveList::GffOutputArchiveList(GffOutputArchive* parent_)
    : parent{parent_}
{
}

GffOutputArchiveStruct& GffOutputArchiveList::push_back(uint32_t id,
    std::initializer_list<std::pair<std::string_view, serialization_cref>> fields)
{
    structs.emplace_back(parent);
    GffOutputArchiveStruct& result = structs.back();
    result.id = id;
    result.add_fields(fields);
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
        [&name](const detail::GffLabel& label) -> bool {
            return label.view() == name;
        });
    if (it == std::end(labels)) {
        labels.emplace_back(name);
        return labels.size() - 1;
    } else {
        return static_cast<size_t>(std::distance(std::begin(labels), it));
    }
}

void GffOutputArchive::build()
{
    // Add and Process top level struct.
    detail::GffStructEntry se;
    se.type = top.id;
    se.field_count = top.field_entries.size();
    se.field_index = build_field_index(top);
    struct_entries.push_back(se);
    ++structs_visited;

    // Build the rest
    build(top);

    header.struct_offset = sizeof(detail::GffHeader);
    header.struct_count = struct_entries.size();
    header.field_offset = header.struct_offset + struct_entries.size() * 12;
    header.field_count = field_entries.size();
    header.label_offset = header.field_offset + field_entries.size() * 12;
    header.label_count = labels.size();
    header.field_data_offset = header.label_offset + labels.size() * 16;
    header.field_data_count = data.size();
    header.field_idx_offset = header.field_data_offset + data.size();
    header.field_idx_count = field_indices.size() * 4;
    header.list_idx_offset = header.field_idx_offset + field_indices.size() * 4;
    header.list_idx_count = list_indices.size() * 4;
}

void GffOutputArchive::build(const GffOutputArchiveField& field)
{
    if (field.type == SerializationType::STRUCT) {
        auto& s = std::get<GffOutputArchiveStruct>(field.structures);
        detail::GffStructEntry se{s.id, build_field_index(s), static_cast<uint32_t>(s.field_entries.size())};
        struct_entries.push_back(se);
        build(s);
    } else if (field.type == SerializationType::LIST) {
        auto& list = std::get<GffOutputArchiveList>(field.structures);
        // Add all the structs
        for (const auto& s : list.structs) {
            struct_entries.push_back({s.id, build_field_index(s), static_cast<uint32_t>(s.field_entries.size())});
        }
        // Process all the structs
        for (const auto& s : list.structs) {
            build(s);
        }
    }
}

void GffOutputArchive::build(const GffOutputArchiveStruct& str)
{
    // Add all the fields.
    for (const auto& f : str.field_entries) {
        field_entries.push_back({f.type, f.label_index, build_field_data(f)});
    }

    // Process the fields
    for (const auto& f : str.field_entries) {
        build(f);
    }
}

uint32_t GffOutputArchive::build_field_data(const GffOutputArchiveField& field)
{
    // NOTE: The data array is built as fields are added.  The only thing that
    // needs to be done is deal with STRUCTs and LISTs
    uint32_t result = 0;
    if (field.type == SerializationType::STRUCT) {
        result = structs_visited;
        ++structs_visited;
    } else if (field.type == SerializationType::LIST) {
        const GffOutputArchiveList& list = std::get<GffOutputArchiveList>(field.structures);
        result = list_indices.size() * 4; // Byte offset
        list_indices.push_back(list.size());
        for (size_t i = 0; i < list.size(); ++i) {
            list_indices.push_back(structs_visited);
            ++structs_visited;
        }
    } else {
        result = field.data_or_offset;
    }
    return result;
}

uint32_t GffOutputArchive::build_field_index(const GffOutputArchiveStruct& str)
{
    uint32_t field_index;
    if (str.field_entries.size() == 1) {
        field_index = fields_visited;
        ++fields_visited;
    } else {
        field_index = field_indices.size() * 4; // Byte offset
        for (size_t i = 0; i < str.field_entries.size(); ++i) {
            field_indices.push_back(fields_visited);
            ++fields_visited;
        }
    }
    return field_index;
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
