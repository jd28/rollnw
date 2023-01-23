#pragma once

#include "../legacy/LocString.hpp"
#include "../resources/Resref.hpp"
#include "../util/ByteArray.hpp"
#include "../util/templates.hpp"

#include <cstdint>
#include <string>
#include <variant>

using namespace std::literals;

namespace nw {

struct GffStruct; // Have to get rid of this.

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
        invalid = std::numeric_limits<uint32_t>::max(),
        uint8 = 0,  // BYTE
        int8 = 1,   // CHAR
        uint16 = 2, // WORD
        int16 = 3,  // SHORT
        uint32 = 4, // DWORD
        int32 = 5,  // INT
        uint64 = 6, // DWORD64
        int64 = 7,
        float_ = 8,
        double_ = 9,
        string = 10, // CEXOSTRING
        resref = 11,
        locstring = 12, // CEXOLOCSTRING
        void_ = 13,
        struct_ = 14,
        list = 15,
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
        return SerializationType::uint8;
    } else if constexpr (std::is_same_v<T, int8_t>) {
        return SerializationType::int8;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return SerializationType::uint16;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return SerializationType::int16;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        return SerializationType::uint32;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return SerializationType::int32;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        return SerializationType::uint64;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return SerializationType::int64;
    } else if constexpr (std::is_same_v<T, float>) {
        return SerializationType::float_;
    } else if constexpr (std::is_same_v<T, double>) {
        return SerializationType::double_;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return SerializationType::string;
    } else if constexpr (std::is_same_v<T, Resref>) {
        return SerializationType::resref;
    } else if constexpr (std::is_same_v<T, LocString>) {
        return SerializationType::locstring;
    } else if constexpr (std::is_same_v<T, ByteArray>) {
        return SerializationType::void_;
    } else if constexpr (std::is_same_v<T, GffStruct>) { // Have to get rid of this somehow
        return SerializationType::struct_;
    } else {
        static_assert(always_false<T>(), "Unknown GFF type.");
    }
}

constexpr std::string_view SerializationType::to_string(SerializationType::type type)
{
    switch (type) {
    default:
        return {};
    case SerializationType::uint8:
        return "byte"sv;
    case SerializationType::int8:
        return "char"sv;
    case SerializationType::uint16:
        return "word"sv;
    case SerializationType::int16:
        return "short"sv;
    case SerializationType::uint32:
        return "dword"sv;
    case SerializationType::int32:
        return "int"sv;
    case SerializationType::uint64:
        return "dword64"sv;
    case SerializationType::int64:
        return "int64"sv;
    case SerializationType::float_:
        return "float"sv;
    case SerializationType::double_:
        return "double"sv;
    case SerializationType::string:
        return "cexostring"sv;
    case SerializationType::resref:
        return "resref"sv;
    case SerializationType::locstring:
        return "cexolocstring"sv;
    case SerializationType::void_:
        return "void"sv;
    case SerializationType::struct_:
        return "struct"sv;
    case SerializationType::list:
        return "list"sv;
    }
}

} // namespace nw
