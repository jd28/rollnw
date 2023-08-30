#pragma once

#include "../i18n/Language.hpp"
#include "../i18n/conversion.hpp"
#include "../legacy/LocString.hpp"
#include "../log.hpp"
#include "../resources/ResourceData.hpp"
#include "../serialization/Serialization.hpp"
#include "../util/macros.hpp"
#include "../util/string.hpp"
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

struct Gff;

// -- GffField ----------------------------------------------------------------
// Note: This is more of an implementation detail.

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
    SerializationType::type type() const;

    /// Get if field is valid
    bool valid() const noexcept { return parent_ != nullptr && entry_ != nullptr; }

    /// If field is a list, return struct at index, else invalid struct.
    GffStruct operator[](size_t index) const;

private:
    friend struct Gff;
    friend struct GffStruct;

    const Gff* parent_ = nullptr;
    const GffFieldEntry* entry_ = nullptr;

    GffField();
    GffField(const Gff* parent, const GffFieldEntry* entry);
};

// -- GffStruct ---------------------------------------------------------------

struct GffStruct {
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
    const GffStructEntry* entry_ = nullptr;

    GffStruct() = default;
    GffStruct(const Gff* parent, const GffStructEntry* entry);
};

struct Gff {
    Gff() = default;
    explicit Gff(const std::filesystem::path& file, nw::LanguageID lang = nw::LanguageID::english);
    explicit Gff(ResourceData data, nw::LanguageID lang = nw::LanguageID::english);

    /// Get the toplevel struct
    GffStruct toplevel() const;

    /// Get Gff type
    std::string_view type() const { return {head_->type, 3}; }

    /// Get if Gff file successfully parsed
    bool valid() const;

    /// Get the Gff Version
    std::string_view version() const { return {head_->version, 4}; }

    GffHeader* head_ = nullptr;
    GffLabel* labels_ = nullptr;
    GffStructEntry* structs_ = nullptr;
    GffFieldEntry* fields_ = nullptr;
    uint32_t* field_indices_ = nullptr;
    uint32_t* list_indices_ = nullptr;

private:
    friend struct GffField;
    friend struct GffStruct;

    ResourceData data_;
    bool is_loaded_ = false;
    nw::LanguageID lang_ = nw::LanguageID::english;

    bool parse();
};

template <typename T>
std::optional<T> GffStruct::get(std::string_view label, bool warn_missing) const
{
    T temp;
    return get_to<T>(label, temp, warn_missing)
        ? std::optional<T>{std::move(temp)}
        : std::optional<T>{};
}

template <typename T>
bool GffStruct::get_to(std::string_view label, T& out, bool warn_missing) const
{
    if (!valid()) {
        return false;
    }
    auto val = (*this)[label];
    if (!val.valid()) {
        if (warn_missing) {
            LOG_F(ERROR, "gff missing field '{}'", label);
        }
        return false;
    }
    T temp;
    if (!val.get_to(temp)) {
        if (warn_missing) {
            LOG_F(ERROR, "gff unable to read field '{}' value", label);
        }
        return false;
    }
    out = std::move(temp);
    return true;
}

template <typename T>
std::optional<T> GffField::get() const
{
    T temp{};
    return get_to(temp) ? std::optional<T>{std::move(temp)} : std::optional<T>{};
}

#define CHECK_OFF(cond)                                              \
    do {                                                             \
        if (!(cond)) {                                               \
            LOG_F(ERROR, "Corrupt GFF: {}", ROLLNW_STRINGIFY(cond)); \
            return false;                                            \
        }                                                            \
    } while (0)

template <typename T>
bool GffField::get_to(T& value) const
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

        if constexpr (std::is_integral_v<T>) {
            int bytes = 0;
            switch (entry_->type) {
            case SerializationType::int8:
            case SerializationType::uint8:
                bytes = 1;
                break;
            case SerializationType::int16:
            case SerializationType::uint16:
                bytes = 2;
                break;
            case SerializationType::int32:
            case SerializationType::uint32:
                bytes = 4;
                break;
            }
            // Only promote iff type is smaller or equal and if the signs are the same
            if (bytes > 0 && entry_->type <= type && entry_->type % 2 == type % 2) {
                // in case not doing it is ub..
                if (entry_->type % 2) {
                    int32_t temp = 0;
                    memcpy(&temp, &entry_->data_or_offset, bytes);
                    value = static_cast<T>(temp);
                } else {
                    uint32_t temp = 0;
                    memcpy(&temp, &entry_->data_or_offset, bytes);
                    value = static_cast<T>(temp);
                }
                return true;
            }
        }

        // Check for exact match on all other types.
        if (entry_->type != type) {
            LOG_F(ERROR, "gff field '{}' types don't match {} != {}", name(), uint32_t(type), entry_->type);
            return false;
        }

        if constexpr (sizeof(T) <= 4) {
            memcpy(&value, &entry_->data_or_offset, sizeof(T));
            return true;
        } else {
            size_t off = entry_->data_or_offset + parent_->head_->field_data_offset;
            CHECK_OFF(off < parent_->data_.bytes.size());

            if constexpr (std::is_same_v<T, Resref>) {
                char buffer[Resref::max_size + 1] = {0};
                uint8_t size = 0;
                CHECK_OFF(parent_->data_.bytes.read_at(off, &size, 1));
                off += 1;
                if (size > Resref::max_size) {
                    LOG_F(ERROR, "gff invalid resref size '{}'", int(size));
                    return false;
                }
                CHECK_OFF(parent_->data_.bytes.read_at(off, buffer, size));
                value = Resref(buffer);
                return true;
            } else if constexpr (std::is_same_v<T, std::string>) {
                uint32_t size;
                CHECK_OFF(parent_->data_.bytes.read_at(off, &size, 4));
                off += 4;
                CHECK_OFF(off + size < parent_->data_.bytes.size());
                std::string s{};
                s.reserve(size + 12); // Reserve a little bit extra, in case of colors.
                s.append(reinterpret_cast<const char*>(parent_->data_.bytes.data() + off), size);
                value = string::sanitize_colors(std::move(s));
                value = to_utf8_by_langid(value, parent_->lang_);
                return true;
            } else if constexpr (std::is_same_v<T, LocString>) {
                uint32_t size, strref, lang, strings;
                CHECK_OFF(parent_->data_.bytes.read_at(off, &size, 4));
                off += 4;
                CHECK_OFF(parent_->data_.bytes.read_at(off, &strref, 4));
                off += 4;
                CHECK_OFF(parent_->data_.bytes.read_at(off, &strings, 4));
                off += 4;

                LocString ls{strref};

                for (uint32_t i = 0; i < strings; ++i) {
                    CHECK_OFF(parent_->data_.bytes.read_at(off, &lang, 4));
                    off += 4;
                    CHECK_OFF(parent_->data_.bytes.read_at(off, &size, 4));
                    off += 4;
                    CHECK_OFF(off + size < parent_->data_.bytes.size());
                    std::string s{reinterpret_cast<const char*>(parent_->data_.bytes.data() + off), size};
                    s = string::sanitize_colors(std::move(s));
                    auto base_lang = Language::to_base_id(lang);
                    s = to_utf8_by_langid(s, base_lang.first);
                    off += size;
                    ls.add(base_lang.first, s, base_lang.second);
                }
                value = std::move(ls);
                return true;
            } else if constexpr (std::is_same_v<T, ByteArray>) {
                uint32_t size;
                parent_->data_.bytes.read_at(off, &size, 4);
                off += 4;
                CHECK_OFF(off + size < parent_->data_.bytes.size());
                value = ByteArray(parent_->data_.bytes.data() + off, size);
                return true;
            } else if constexpr (std::is_same_v<T, GffStruct>) {
                if (entry_->data_or_offset < parent_->head_->struct_count) {
                    value = GffStruct(parent_, &parent_->structs_[entry_->data_or_offset]);
                } else {
                    LOG_F(ERROR, "GffField: Invalid index ({}) into struct array", entry_->data_or_offset);
                    value = GffStruct();
                }
                return true;
            } else {
                T temp;
                CHECK_OFF(parent_->data_.bytes.read_at(off, &temp, sizeof(T)));
                return true;
            }
        }
    }
}

#undef CHECK_OFF

} // namespace NW
