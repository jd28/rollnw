#pragma once

#include "../i18n/LocString.hpp"
#include "../resources/Resource.hpp"
#include "../util/ByteArray.hpp"

#include <cstdint>
#include <type_traits>

namespace nw {

struct GffInputArchiveStruct;

namespace detail {

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

} // namespace nw
