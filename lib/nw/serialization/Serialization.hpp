#pragma once

#include "../i18n/LocString.hpp"
#include "../resources/Resref.hpp"
#include "../util/ByteArray.hpp"
#include "../util/templates.hpp"

#include <cstdint>
#include <string>
#include <variant>

using namespace std::literals;

namespace nw {

struct GffInputArchiveStruct; // Have to get rid of this.

// -- SerializationProfile ----------------------------------------------------

/// Game serialization profiles
enum struct SerializationProfile {
    any,
    blueprint,
    instance,
    savegame
};

// -- SerializationType -------------------------------------------------------

/// Gff types, renamed for clarity
struct SerializationType {
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

    /// Convert type to SerializationType
    template <typename T>
    static constexpr SerializationType::type id();

    static constexpr std::string_view to_string(SerializationType::type type);
};

template <typename T>
constexpr SerializationType::type SerializationType::id()
{
    if constexpr (std::is_same_v<T, uint8_t>) {
        return SerializationType::UINT8;
    } else if constexpr (std::is_same_v<T, int8_t>) {
        return SerializationType::INT8;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return SerializationType::UINT16;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return SerializationType::INT16;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        return SerializationType::UINT32;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return SerializationType::INT32;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        return SerializationType::UINT64;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return SerializationType::INT64;
    } else if constexpr (std::is_same_v<T, float>) {
        return SerializationType::FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
        return SerializationType::DOUBLE;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return SerializationType::STRING;
    } else if constexpr (std::is_same_v<T, Resref>) {
        return SerializationType::RESREF;
    } else if constexpr (std::is_same_v<T, LocString>) {
        return SerializationType::LOCSTRING;
    } else if constexpr (std::is_same_v<T, ByteArray>) {
        return SerializationType::VOID;
    } else if constexpr (std::is_same_v<T, GffInputArchiveStruct>) { // Have to get rid of this somehow
        return SerializationType::STRUCT;
    } else {
        static_assert(always_false<T>(), "Unknown GFF type.");
    }
}

constexpr std::string_view SerializationType::to_string(SerializationType::type type)
{
    switch (type) {
    default:
        return {};
    case SerializationType::UINT8:
        return "byte"sv;
    case SerializationType::INT8:
        return "char"sv;
    case SerializationType::UINT16:
        return "word"sv;
    case SerializationType::INT16:
        return "short"sv;
    case SerializationType::UINT32:
        return "dword"sv;
    case SerializationType::INT32:
        return "int"sv;
    case SerializationType::UINT64:
        return "dword64"sv;
    case SerializationType::INT64:
        return "int64"sv;
    case SerializationType::FLOAT:
        return "float"sv;
    case SerializationType::DOUBLE:
        return "double"sv;
    case SerializationType::STRING:
        return "cexostring"sv;
    case SerializationType::RESREF:
        return "resref"sv;
    case SerializationType::LOCSTRING:
        return "cexolocstring"sv;
    case SerializationType::VOID:
        return "void"sv;
    case SerializationType::STRUCT:
        return "struct"sv;
    case SerializationType::LIST:
        return "list"sv;
    }
}

} // namespace nw
