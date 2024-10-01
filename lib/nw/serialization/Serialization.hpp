#pragma once

#include "../i18n/LocString.hpp"
#include "../resources/Resref.hpp"
#include "../util/ByteArray.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <limits>
#include <string>

// Forward decls;

namespace nw {
struct Gff;
struct GffBuilder;
struct GffBuilderStruct;
struct GffStruct;
} // namespace nw

namespace nw {

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

    static constexpr StringView to_string(SerializationType::type type);
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
    } else if constexpr (std::is_same_v<T, String>) {
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

constexpr StringView SerializationType::to_string(SerializationType::type type)
{
    switch (type) {
    default:
        return {};
    case SerializationType::uint8:
        return "byte";
    case SerializationType::int8:
        return "char";
    case SerializationType::uint16:
        return "word";
    case SerializationType::int16:
        return "short";
    case SerializationType::uint32:
        return "dword";
    case SerializationType::int32:
        return "int";
    case SerializationType::uint64:
        return "dword64";
    case SerializationType::int64:
        return "int64";
    case SerializationType::float_:
        return "float";
    case SerializationType::double_:
        return "double";
    case SerializationType::string:
        return "cexostring";
    case SerializationType::resref:
        return "resref";
    case SerializationType::locstring:
        return "cexolocstring";
    case SerializationType::void_:
        return "void";
    case SerializationType::struct_:
        return "struct";
    case SerializationType::list:
        return "list";
    }
}

} // namespace nw
