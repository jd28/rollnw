#pragma once

#include "../i18n/conversion.hpp"
#include "../serialization/Serialization.hpp"
#include "../util/templates.hpp"
#include "gff_common.hpp"

#include <cstdint>

namespace nw {

// Not super concerned about the efficiency of this, only the correctness.  Ideally, the way it's written,
// will be exactly the way an object reads it.

struct GffBuilder;
struct GffBuilderField;
struct GffBuilderList;
struct GffBuilderStruct;

struct GffBuilderStruct {
    GffBuilderStruct() = default;
    explicit GffBuilderStruct(GffBuilder* parent_);

    template <typename T>
    GffBuilderStruct& add_field(std::string_view name, const T& value);
    GffBuilderList& add_list(std::string_view name);
    GffBuilderStruct& add_struct(std::string_view name, uint32_t id_);

    GffBuilder* parent = nullptr;
    uint32_t index = 0;
    uint32_t id = 0;
    std::vector<GffBuilderField> field_entries;
};

struct GffBuilderList {
    GffBuilderList() = default;
    explicit GffBuilderList(GffBuilder* parent_);

    GffBuilderStruct& push_back(uint32_t id);
    size_t size() const noexcept { return structs.size(); }

    GffBuilder* parent = nullptr;
    std::vector<GffBuilderStruct> structs;
};

struct GffBuilderField {
    explicit GffBuilderField(GffBuilder* parent_);

    GffBuilder* parent = nullptr;
    SerializationType::type type;
    uint32_t index = 0;
    uint32_t label_index = 0;
    uint32_t data_or_offset = 0;
    std::variant<GffBuilderStruct, GffBuilderList> structures;
};

struct GffBuilder {
    explicit GffBuilder(std::string_view type, std::string_view version = "V3.2");

    size_t add_label(std::string_view name);
    // Note, this must be called before `write_to` or `to_byte_array`
    void build();
    ByteArray to_byte_array() const;
    bool write_to(const std::filesystem::path& path) const;

    GffBuilderStruct top;

    GffHeader header;
    ByteArray data;
    std::vector<GffLabel> labels;
    std::vector<uint32_t> field_indices;
    std::vector<uint32_t> list_indices;
    std::vector<GffFieldEntry> field_entries;
    std::vector<GffStructEntry> struct_entries;
};

template <typename T>
GffBuilderStruct& GffBuilderStruct::add_field(std::string_view name, const T& value)
{
    GffBuilderField f{parent};
    f.label_index = static_cast<uint32_t>(parent->add_label(name));

    if constexpr (std::is_enum_v<T>) {
        return add_field(name, to_underlying(value));
    } else if constexpr (std::is_same_v<T, bool>) {
        f.type = SerializationType::id<uint8_t>();
        uint8_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 1);
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        f.type = SerializationType::id<uint8_t>();
        uint8_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 1);
    } else if constexpr (std::is_same_v<T, int8_t>) {
        f.type = SerializationType::id<int8_t>();
        int8_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 1);
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        f.type = SerializationType::id<uint16_t>();
        uint16_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 2);
    } else if constexpr (std::is_same_v<T, int16_t>) {
        f.type = SerializationType::id<int16_t>();
        int16_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 2);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        f.type = SerializationType::id<uint32_t>();
        uint32_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 4);
    } else if constexpr (std::is_same_v<T, int32_t>) {
        f.type = SerializationType::id<int32_t>();
        int32_t temp = value;
        std::memcpy(&f.data_or_offset, &temp, 4);
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        f.type = SerializationType::id<uint64_t>();
        uint64_t temp = value;
        parent->data.append(&temp, 8);
    } else if constexpr (std::is_same_v<T, int64_t>) {
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        f.type = SerializationType::id<int64_t>();
        int64_t temp = value;
        parent->data.append(&temp, 8);
    } else if constexpr (std::is_same_v<T, float>) {
        f.type = SerializationType::id<float>();
        float temp = value;
        std::memcpy(&f.data_or_offset, &temp, 4);
    } else if constexpr (std::is_same_v<T, double>) {
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        f.type = SerializationType::id<double>();
        double temp = value;
        parent->data.append(&temp, 8);
    } else if constexpr (std::is_same_v<T, std::string>) {
        const std::string& temp = value;
        f.type = SerializationType::id<std::string>();
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        std::string s = string::desanitize_colors(temp);
        s = from_utf8_by_global_lang(s);
        uint32_t size = static_cast<uint32_t>(s.size());
        parent->data.append(&size, 4);
        parent->data.append(s.data(), size);
    } else if constexpr (std::is_same_v<T, Resref>) {
        const Resref& temp = value;
        f.type = SerializationType::id<Resref>();
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        uint8_t size = static_cast<uint8_t>(temp.length());
        parent->data.append(&size, 1);
        parent->data.append(temp.view().data(), size);
    } else if constexpr (std::is_same_v<T, LocString>) {
        const LocString& temp = value;
        f.type = SerializationType::id<LocString>();
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        uint32_t total_size = 8;
        uint32_t strref = temp.strref(), num_strings = static_cast<uint32_t>(temp.size());
        size_t placeholder = parent->data.size(); // Won't know total size till the end.
        parent->data.append(&total_size, 4);
        parent->data.append(&strref, 4);
        parent->data.append(&num_strings, 4);
        for (const auto& [lang, str] : temp) {
            std::string s = string::desanitize_colors(str);
            auto base_lang = Language::to_base_id(lang);
            s = from_utf8_by_langid(s, base_lang.first);
            uint32_t size = static_cast<uint32_t>(s.size());
            total_size += 8 + size;
            parent->data.append(&lang, 4);
            parent->data.append(&size, 4);
            parent->data.append(s.data(), size);
        }
        memcpy(parent->data.data() + placeholder, &total_size, 4);
    } else if constexpr (std::is_same_v<T, ByteArray>) {
        const ByteArray& temp = value;
        f.type = SerializationType::id<ByteArray>();
        f.data_or_offset = static_cast<uint32_t>(parent->data.size());
        uint32_t size = static_cast<uint32_t>(temp.size());
        parent->data.append(&size, 4);
        parent->data.append(temp.data(), size);
    } else {
        static_assert(always_false<T>());
    }
    field_entries.push_back(f);

    return *this;
}

} // namespace nw
