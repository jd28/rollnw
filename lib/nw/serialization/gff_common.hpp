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
    } else if constexpr (std::is_same_v<T, GffInputArchiveStruct>) {
        return GffType::STRUCT;
    } else {
        static_assert(detail::always_false<T>(), "Unknown GFF type.");
    }
}

inline const char* gff_type_to_string(GffType::type type)
{
    switch (type) {
    default:
        return "(invalid)";
    case GffType::UINT8:
        return "byte";
    case GffType::INT8:
        return "char";
    case GffType::UINT16:
        return "word";
    case GffType::INT16:
        return "short";
    case GffType::UINT32:
        return "dword";
    case GffType::INT32:
        return "int";
    case GffType::UINT64:
        return "dword64";
    case GffType::INT64:
        return "int64";
    case GffType::FLOAT:
        return "float";
    case GffType::DOUBLE:
        return "double";
    case GffType::STRING:
        return "cexostring";
    case GffType::RESREF:
        return "resref";
    case GffType::LOCSTRING:
        return "cexolocstring";
    case GffType::VOID:
        return "void";
    case GffType::STRUCT:
        return "struct";
    case GffType::LIST:
        return "list";
    }
}

} // namespace nw
