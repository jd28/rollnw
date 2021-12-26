#pragma once

#include "Serialization.hpp"
#include "gff_common.hpp"

namespace nw {

// There are a couple different approaches one can take here, either depth or breadth first.
// The former has been opted for here, tho it seems likely given the nature of the GFF file format,
// it was originally designed around a breadth first approach.

struct GffOutputArchive;
struct GffOutputArchiveField;
struct GffOutputArchiveList;
struct GffOutputArchiveStruct;

struct GffOutputArchiveStruct {
    GffOutputArchiveStruct() = default;
    explicit GffOutputArchiveStruct(GffOutputArchive* parent_);

    void add_field(std::string_view name, serialization_cref value);
    void add_fields(std::initializer_list<std::pair<std::string_view, serialization_cref>> fields);
    GffOutputArchiveList& add_list(std::string_view name);
    GffOutputArchiveStruct& add_struct(std::string_view name, uint32_t id);

    GffOutputArchive* parent = nullptr;
    uint32_t id = 0;
    std::vector<GffOutputArchiveField> field_entries;
};

struct GffOutputArchiveList {
    GffOutputArchiveList() = default;
    explicit GffOutputArchiveList(GffOutputArchive* parent_);

    GffOutputArchiveStruct& push_back(uint32_t id,
        std::initializer_list<std::pair<std::string_view, serialization_cref>> fields);
    size_t size() const noexcept { return structs.size(); }

    GffOutputArchive* parent = nullptr;
    std::vector<GffOutputArchiveStruct> structs;
};

struct GffOutputArchiveField {
    explicit GffOutputArchiveField(GffOutputArchive* parent_);

    GffOutputArchive* parent = nullptr;
    SerializationType::type type;
    uint32_t label_index = 0;
    uint32_t data_or_offset = 0;
    std::variant<GffOutputArchiveStruct, GffOutputArchiveList> structures;
};

struct GffOutputArchive {
    explicit GffOutputArchive(std::string_view type, std::string_view version = "V3.2");

    size_t add_label(std::string_view name);
    void build();
    void build(const GffOutputArchiveField& field);
    void build(const GffOutputArchiveStruct& str);
    uint32_t build_field_data(const GffOutputArchiveField& field);
    uint32_t build_field_index(const GffOutputArchiveStruct& str);

    bool write_to(const std::filesystem::path& path) const;

    GffOutputArchiveStruct top;

    detail::GffHeader header;
    ByteArray data;
    std::vector<detail::GffLabel> labels;
    std::vector<uint32_t> field_indices;
    std::vector<uint32_t> list_indices;
    std::vector<detail::GffFieldEntry> field_entries;
    std::vector<detail::GffStructEntry> struct_entries;
    uint32_t structs_visited = 0;
    uint32_t fields_visited = 0;
};

} // namespace nw
