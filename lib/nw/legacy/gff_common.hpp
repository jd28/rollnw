#pragma once

#include "../legacy/LocString.hpp"
#include "../resources/Resource.hpp"
#include "../util/ByteArray.hpp"

#include <cstdint>
#include <type_traits>

namespace nw {

struct GffStruct;

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

struct GffLabel {
    static constexpr size_t max_size = 16;
    using Storage = std::array<char, max_size>;
    using value_type = typename Storage::value_type;
    using size_type = typename Storage::size_type;

    GffLabel();
    GffLabel(const GffLabel&) = default;
    GffLabel(Storage data) noexcept;
    GffLabel(const char* string) noexcept;
    GffLabel(std::string_view string) noexcept;

    GffLabel& operator=(const GffLabel&) = default;

    /// Checks if the underlying array has no non-null characters
    bool empty() const noexcept;

    /// Returns the number of char elements in the array, excluding nulls.
    size_type length() const noexcept;

    /// Creates ``std::string`` of underlying array
    std::string string() const;

    /// Creates ``std::string_view`` of underlying array without null padding
    std::string_view view() const noexcept;

private:
    Storage data_;
};

} // namespace nw
