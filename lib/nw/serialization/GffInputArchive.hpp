#pragma once

#include "../i18n/Language.hpp"
#include "../i18n/LocString.hpp"
#include "../i18n/conversion.hpp"
#include "../log.hpp"
#include "../resources/Resource.hpp"
#include "../util/ByteArray.hpp"
#include "../util/macros.hpp"
#include "../util/string.hpp"
#include "Serialization.hpp"
#include "gff_common.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nw {

struct GffInputArchive;

// -- GffInputArchiveField ----------------------------------------------------------------
// Note: This is more of an implementation detail.

struct GffInputArchiveField {
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
    SerializationType::type type() const;

    /// Get if field is valid
    bool valid() const noexcept { return parent_ != nullptr && entry_ != nullptr; }

    /// If field is a list, return struct at index, else invalid struct.
    GffInputArchiveStruct operator[](size_t index) const;

private:
    friend struct GffInputArchive;
    friend struct GffInputArchiveStruct;

    const GffInputArchive* parent_ = nullptr;
    const detail::GffFieldEntry* entry_ = nullptr;

    GffInputArchiveField();
    GffInputArchiveField(const GffInputArchive* parent, const detail::GffFieldEntry* entry);
};

// -- GffInputArchiveStruct ----------------------------------------------------------------

struct GffInputArchiveStruct {
    /// Check if a struct has a field.
    bool has_field(std::string_view label) const;

    /// Get struct id.
    uint32_t id() const { return valid() ? entry_->type : 0; }

    /// Gets a value from a field in the struct
    template <typename T>
    std::optional<T> get(std::string_view label, bool warn_missing = true) const;

    /// Gets a value from a field in the struct
    template <typename T>
    bool get_to(std::string_view label, T& out, bool warn_missing = true) const;

    /// Number of fields in the struct.
    size_t size() const { return entry_->field_count; }

    /// Check if GffInputArchive has been parsed without error.
    bool valid() const { return parent_ != nullptr; }

    /// Get field by label
    GffInputArchiveField operator[](std::string_view label) const;

    /// Get field by index
    GffInputArchiveField operator[](size_t index) const;

private:
    friend struct GffInputArchive;
    friend struct GffInputArchiveField;

    const GffInputArchive* parent_ = nullptr;
    const detail::GffStructEntry* entry_ = nullptr;

    GffInputArchiveStruct() = default;
    GffInputArchiveStruct(const GffInputArchive* parent, const detail::GffStructEntry* entry);
};

struct GffInputArchive {
    GffInputArchive();
    explicit GffInputArchive(const std::filesystem::path& fileName);
    explicit GffInputArchive(ByteArray bytes);

    /// Get the toplevel struct
    GffInputArchiveStruct toplevel() const;

    /// Get Gff type
    std::string_view type() const { return {head_->type, 3}; }

    /// Get if Gff file successfully parsed
    bool valid() const;

    /// Get the Gff Version
    std::string_view version() const { return {head_->version, 4}; }

private:
    friend struct GffInputArchiveField;
    friend struct GffInputArchiveStruct;

    detail::GffHeader* head_ = nullptr;
    Resref* labels_ = nullptr; // Not techinically resref, but fine for now.
    detail::GffStructEntry* structs_ = nullptr;
    detail::GffFieldEntry* fields_ = nullptr;
    uint32_t* field_indices_ = nullptr;
    uint32_t* list_indices_ = nullptr;

    ByteArray bytes_;
    bool is_loaded_ = false;

    bool parse();
};

template <typename T>
std::optional<T> GffInputArchiveStruct::get(std::string_view label, bool warn_missing) const
{
    T temp;
    return get_to(label, temp, warn_missing)
        ? std::optional<T>{std::move(temp)}
        : std::optional<T>{};
}

template <typename T>
bool GffInputArchiveStruct::get_to(std::string_view label, T& out, bool warn_missing) const
{
    if (!valid()) { return false; }
    auto val = (*this)[label];
    if (!val.valid()) {
        if (warn_missing) { LOG_F(ERROR, "gff missing field '{}'", label); }
        return false;
    }
    T temp;
    if (!val.get_to(temp)) {
        if (warn_missing) { LOG_F(ERROR, "gff unable to read field '{}' value", label); }
        return false;
    }
    out = std::move(temp);
    return true;
}

template <typename T>
std::optional<T> GffInputArchiveField::get() const
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
bool GffInputArchiveField::get_to(T& value) const
{
    if (!valid()) {
        LOG_F(ERROR, "invalid gff field");
        return false;
    }

    if constexpr (std::is_enum_v<T>) {
        std::underlying_type_t<T> temp;
        bool r = get_to(temp);
        if (r) {
            value = static_cast<T>(temp);
            return true;
        } else {
            return false;
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        uint8_t temp = 0;
        if (get_to(temp)) {
            value = !!temp;
            return true;
        } else {
            return false;
        }

    } else {
        SerializationType::type type = SerializationType::id<T>();
        if (entry_->type != type) {
            LOG_F(ERROR, "gff field '{}' types don't match {} != {}", name(), type, entry_->type);
            return false;
        }

        if constexpr (sizeof(T) <= 4) {
            memcpy(&value, &entry_->data_or_offset, sizeof(T));
            return true;
        } else {
            size_t off = entry_->data_or_offset + parent_->head_->field_data_offset;

            if constexpr (std::is_same_v<T, Resref>) {
                char buffer[17] = {0};
                uint8_t size = 0;
                CHECK_OFF(parent_->bytes_.read_at(off, &size, 1));
                off += 1;
                CHECK_OFF(parent_->bytes_.read_at(off, buffer, size));
                value = Resref(buffer);
            } else if constexpr (std::is_same_v<T, std::string>) {
                uint32_t size;
                parent_->bytes_.read_at(off, &size, 4);
                off += 4;
                CHECK_OFF(off + size < parent_->bytes_.size());
                std::string s{};
                s.reserve(size + 12); // Reserve a little bit extra, in case of colors.
                s.append((char*)parent_->bytes_.data() + off, size);
                value = string::sanitize_colors(std::move(s));
                value = to_utf8(value, Language::default_encoding(), true);
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
                    s = to_utf8_by_langid(s, static_cast<Language::ID>(lang), true);
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
            } else if constexpr (std::is_same_v<T, GffInputArchiveStruct>) {
                if (entry_->data_or_offset < parent_->head_->struct_count) {
                    value = GffInputArchiveStruct(parent_, &parent_->structs_[entry_->data_or_offset]);
                } else {
                    LOG_F(ERROR, "GffField: Invalid index ({}) into struct array", entry_->data_or_offset);
                    value = GffInputArchiveStruct();
                }
            } else {
                return false;
            }
        }
        return true;
    }
}

#undef CHECK_OFF

} // namespace NW
