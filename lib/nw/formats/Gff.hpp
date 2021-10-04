#pragma once

#include "../i18n/Language.hpp"
#include "../i18n/LocString.hpp"
#include "../i18n/conversion.hpp"
#include "../log.hpp"
#include "../resources/Resource.hpp"
#include "../util/ByteArray.hpp"
#include "../util/macros.hpp"
#include "../util/string.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nw {

struct Gff;
struct GffStruct;

namespace detail {

template <typename T>
constexpr bool always_false() { return false; }

struct GffHeader {
    char type[4];
    char version[4];
    uint32_t struct_offset;
    uint32_t struct_count;
    uint32_t field_offset;
    uint32_t field_count;
    uint32_t label_offset;
    uint32_t label_count;
    uint32_t field_data_offset;
    uint32_t field_data_count;
    uint32_t field_idx_offset;
    uint32_t field_idx_count;
    uint32_t list_idx_offset;
    uint32_t list_idx_count;
};

struct GffFieldEntry {
    uint32_t type;
    uint32_t label_idx;
    uint32_t data_or_offset;
};

struct GffStructEntry {
    uint32_t type;
    uint32_t field_index;
    uint32_t field_count;
};

} // namespace detail

// -- GffType ----------------------------------------------------------------

struct GffType {
    /// Gff types, renamed for clarity
    enum type : uint32_t {
        INVALID = std::numeric_limits<uint32_t>::max(),
        UINT8 = 0,  // BYTE
        INT8 = 1,   // CHAR
        UINT16 = 2, // WORD
        INT16 = 3,  // SHORT
        UINT32 = 4, // DWORD
        INT32 = 5,  // INT
        UINT64 = 6, // DWORD64
        INT64 = 7,
        FLOAT = 8,
        DOUBLE = 9,
        STRING = 10, // CEXOSTRING
        RESREF = 11,
        LOCSTRING = 12, // CEXOLOCSTRING
        VOID = 13,
        STRUCT = 14,
        LIST = 15,
    };

    /// Convert type to GffType
    template <typename T>
    static constexpr GffType::type id();
};

template <typename T>
constexpr GffType::type GffType::id()
{
    if constexpr (std::is_same_v<T, uint8_t>) {
        return GffType::UINT8;
    } else if constexpr (std::is_same_v<T, int8_t>) {
        return GffType::INT8;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return GffType::UINT16;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return GffType::INT16;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        return GffType::UINT32;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return GffType::INT32;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        return GffType::UINT64;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return GffType::INT64;
    } else if constexpr (std::is_same_v<T, float>) {
        return GffType::FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
        return GffType::DOUBLE;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return GffType::STRING;
    } else if constexpr (std::is_same_v<T, Resref>) {
        return GffType::RESREF;
    } else if constexpr (std::is_same_v<T, LocString>) {
        return GffType::LOCSTRING;
    } else if constexpr (std::is_same_v<T, ByteArray>) {
        return GffType::VOID;
    } else if constexpr (std::is_same_v<T, GffStruct>) {
        return GffType::STRUCT;
    } else {
        static_assert(detail::always_false<T>(), "Unknown GFF type.");
    }
}

// -- GffField ----------------------------------------------------------------

struct GffField {
    /// Get the field's value
    template <typename T>
    std::optional<T> get() const;

    /// Get the field's value
    template <typename T>
    bool get_to(T& value) const;

    /// Get label
    std::string_view name() const;

    /// If field is a list, returns size of list, else 0.
    size_t size() const;

    /// Get field type
    GffType::type type() const;

    /// Get if field is valid
    bool valid() const { return parent_ != nullptr; }

    /// If field is a list, return struct at index, else invalid struct.
    GffStruct operator[](size_t index) const;

private:
    friend struct Gff;
    friend struct GffStruct;

    const Gff* parent_ = nullptr;
    const detail::GffFieldEntry* entry_ = nullptr;

    GffField();
    GffField(const Gff* parent, const detail::GffFieldEntry* entry);
};

// -- GffStruct ----------------------------------------------------------------

struct GffStruct {
    /// Check if a struct has a field.
    bool has_field(std::string_view label) const;

    /// Get struct id.
    uint32_t id() const { return entry_->type; }

    /// Number of fields in the struct.
    size_t size() const { return entry_->field_count; }

    /// Check if Gff has been parsed without error.
    bool valid() const { return parent_ != nullptr; }

    /// Get field by label
    GffField operator[](std::string_view label) const;

    /// Get field by index
    GffField operator[](size_t index) const;

private:
    friend struct Gff;
    friend struct GffField;

    const Gff* parent_ = nullptr;
    const detail::GffStructEntry* entry_ = nullptr;

    GffStruct() = default;
    GffStruct(const Gff* parent, const detail::GffStructEntry* entry);
};

struct Gff {
    Gff();
    Gff(const std::filesystem::path& fileName);
    Gff(ByteArray bytes);

    /// Get the toplevel struct
    GffStruct toplevel() const;

    /// Get Gff type
    std::string_view type() const { return {head_->type, 3}; }

    /// Get if Gff file successfully parsed
    bool valid() const;

    /// Get the Gff Version
    std::string_view version() const { return {head_->version, 4}; }

    /// Helper to get top level fields
    GffField operator[](std::string_view label) const { return toplevel()[label]; }

    /// Helper to get top level fields
    GffField operator[](size_t index) const { return toplevel()[index]; }

private:
    friend struct GffField;
    friend struct GffStruct;

    detail::GffHeader* head_;
    Resref* labels_; // Not techinically resref, but fine for now.
    detail::GffStructEntry* structs_;
    detail::GffFieldEntry* fields_;
    uint32_t* field_indices_;
    uint32_t* list_indices_;

    ByteArray bytes_;
    bool is_loaded_ = false;

    bool parse();
};

template <typename T>
std::optional<T> GffField::get() const
{
    T temp;
    return get_to(temp) ? std::optional<T>{std::move(temp)} : std::optional<T>{};
}

#define CHECK_OFF(cond)                                             \
    do {                                                            \
        if (!(cond)) {                                              \
            LOG_F(ERROR, "Corrupt GFF: {}", LIBNW_STRINGIFY(cond)); \
            return false;                                           \
        }                                                           \
    } while (0)

template <typename T>
bool GffField::get_to(T& value) const
{
    GffType::type type = GffType::id<T>();
    if (entry_->type != type) {
        LOG_F(ERROR, "Gff Types don't match {} != {}", type, entry_->type);
        return false;
    }

    if constexpr (sizeof(T) <= 4) {
        value = T(entry_->data_or_offset);
        return true;
    } else {
        size_t off = entry_->data_or_offset + parent_->head_->field_data_offset;

        if constexpr (std::is_same_v<T, Resref>) {
            char buffer[17] = {0};
            uint8_t size;
            parent_->bytes_.read_at(off, &size, 1);
            off += 1;
            CHECK_OFF(parent_->bytes_.read_at(off, buffer, size));
            value = Resref(buffer);
        } else if constexpr (std::is_same_v<T, std::string>) {
            uint32_t size;
            parent_->bytes_.read_at(off, &size, 4);
            off += 4;
            CHECK_OFF(off + size < parent_->bytes_.size());
            value = std::string((char*)parent_->bytes_.data() + off, size);
            value = string::sanitize_colors(std::move(value));
            value = to_utf8(value, Language::default_encoding(), false);
        } else if constexpr (std::is_same_v<T, LocString>) {
            uint32_t size, strref, lang, strings;
            parent_->bytes_.read_at(off, &size, 4);
            off += 4;
            parent_->bytes_.read_at(off, &strref, 4);
            off += 4;
            parent_->bytes_.read_at(off, &strings, 4);
            off += 4;

            LocString ls{strref};

            for (uint32_t i = 0; i < strings; ++i) {
                parent_->bytes_.read_at(off, &lang, 4);
                off += 4;
                parent_->bytes_.read_at(off, &size, 4);
                off += 4;
                CHECK_OFF(off + size < parent_->bytes_.size());
                std::string s{(char*)parent_->bytes_.data() + off, size};
                s = string::sanitize_colors(std::move(s));
                s = to_utf8_by_langid(s, static_cast<Language::ID>(lang), false);
                off += size;
                ls.add(lang, std::move(s));
            }
            value = std::move(ls);
        } else if constexpr (std::is_same_v<T, ByteArray>) {
            uint32_t size;
            parent_->bytes_.read_at(off, &size, 4);
            off += 4;
            CHECK_OFF(off + size < parent_->bytes_.size());
            value = ByteArray(parent_->bytes_.data() + off, size);
        } else if constexpr (std::is_same_v<T, GffStruct>) {
            if (entry_->data_or_offset < parent_->head_->struct_count) {
                value = GffStruct(parent_, &parent_->structs_[entry_->data_or_offset]);
            } else {
                LOG_F(ERROR, "GffField: Invalid index ({}) into struct array", entry_->data_or_offset);
                value = GffStruct();
            }
        } else {
            parent_->bytes_.read_at(off, &value, sizeof(T));
        }
    }
    return true;
}

#undef CHECK_OFF

} // namespace NW
