#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace nw::smalls {

enum class IntrinsicId : uint16_t {
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    BitShl,
    BitShr,

    ArrayPush,
    ArrayPop,
    ArrayLen,
    ArrayClear,
    ArrayReserve,
    ArrayGet,
    ArraySet,

    MapLen,
    MapGet,
    MapSet,
    MapHas,
    MapRemove,
    MapClear,
    MapIterBegin,
    MapIterNext,
    MapIterEnd,

    StringLen,
    StringSubstr,
    StringCharAt,
    StringFind,
    StringContains,
    StringStartsWith,
    StringEndsWith,
    StringToUpper,
    StringToLower,
    StringTrim,
    StringReplace,
    StringSplit,
    StringJoin,
    StringToInt,
    StringToFloat,
    StringFromCharCode,
    StringConcat,
    StringAppend,
    StringInsert,
    StringReverse,

    Invalid = 0xFFFF
};

inline std::optional<IntrinsicId> intrinsic_id_from_string(std::string_view name)
{
    if (name == "bit_and") {
        return IntrinsicId::BitAnd;
    } else if (name == "bit_or") {
        return IntrinsicId::BitOr;
    } else if (name == "bit_xor") {
        return IntrinsicId::BitXor;
    } else if (name == "bit_not") {
        return IntrinsicId::BitNot;
    } else if (name == "bit_shl") {
        return IntrinsicId::BitShl;
    } else if (name == "bit_shr") {
        return IntrinsicId::BitShr;
    } else if (name == "array_push") {
        return IntrinsicId::ArrayPush;
    } else if (name == "array_pop") {
        return IntrinsicId::ArrayPop;
    } else if (name == "array_len") {
        return IntrinsicId::ArrayLen;
    } else if (name == "array_clear") {
        return IntrinsicId::ArrayClear;
    } else if (name == "array_reserve") {
        return IntrinsicId::ArrayReserve;
    } else if (name == "array_get") {
        return IntrinsicId::ArrayGet;
    } else if (name == "array_set") {
        return IntrinsicId::ArraySet;
    } else if (name == "map_len") {
        return IntrinsicId::MapLen;
    } else if (name == "map_get") {
        return IntrinsicId::MapGet;
    } else if (name == "map_set") {
        return IntrinsicId::MapSet;
    } else if (name == "map_has") {
        return IntrinsicId::MapHas;
    } else if (name == "map_remove") {
        return IntrinsicId::MapRemove;
    } else if (name == "map_clear") {
        return IntrinsicId::MapClear;
    } else if (name == "map_iter_begin") {
        return IntrinsicId::MapIterBegin;
    } else if (name == "map_iter_next") {
        return IntrinsicId::MapIterNext;
    } else if (name == "map_iter_end") {
        return IntrinsicId::MapIterEnd;
    } else if (name == "string_len") {
        return IntrinsicId::StringLen;
    } else if (name == "string_substr") {
        return IntrinsicId::StringSubstr;
    } else if (name == "string_char_at") {
        return IntrinsicId::StringCharAt;
    } else if (name == "string_find") {
        return IntrinsicId::StringFind;
    } else if (name == "string_contains") {
        return IntrinsicId::StringContains;
    } else if (name == "string_starts_with") {
        return IntrinsicId::StringStartsWith;
    } else if (name == "string_ends_with") {
        return IntrinsicId::StringEndsWith;
    } else if (name == "string_to_upper") {
        return IntrinsicId::StringToUpper;
    } else if (name == "string_to_lower") {
        return IntrinsicId::StringToLower;
    } else if (name == "string_trim") {
        return IntrinsicId::StringTrim;
    } else if (name == "string_replace") {
        return IntrinsicId::StringReplace;
    } else if (name == "string_split") {
        return IntrinsicId::StringSplit;
    } else if (name == "string_join") {
        return IntrinsicId::StringJoin;
    } else if (name == "string_to_int") {
        return IntrinsicId::StringToInt;
    } else if (name == "string_to_float") {
        return IntrinsicId::StringToFloat;
    } else if (name == "string_from_char_code") {
        return IntrinsicId::StringFromCharCode;
    } else if (name == "string_concat") {
        return IntrinsicId::StringConcat;
    } else if (name == "string_append") {
        return IntrinsicId::StringAppend;
    } else if (name == "string_insert") {
        return IntrinsicId::StringInsert;
    } else if (name == "string_reverse") {
        return IntrinsicId::StringReverse;
    }

    return std::nullopt;
}

inline std::string_view intrinsic_name(IntrinsicId id)
{
    switch (id) {
    case IntrinsicId::BitAnd:
        return "bit_and";
    case IntrinsicId::BitOr:
        return "bit_or";
    case IntrinsicId::BitXor:
        return "bit_xor";
    case IntrinsicId::BitNot:
        return "bit_not";
    case IntrinsicId::BitShl:
        return "bit_shl";
    case IntrinsicId::BitShr:
        return "bit_shr";
    case IntrinsicId::ArrayPush:
        return "array_push";
    case IntrinsicId::ArrayPop:
        return "array_pop";
    case IntrinsicId::ArrayLen:
        return "array_len";
    case IntrinsicId::ArrayClear:
        return "array_clear";
    case IntrinsicId::ArrayReserve:
        return "array_reserve";
    case IntrinsicId::ArrayGet:
        return "array_get";
    case IntrinsicId::ArraySet:
        return "array_set";
    case IntrinsicId::MapLen:
        return "map_len";
    case IntrinsicId::MapGet:
        return "map_get";
    case IntrinsicId::MapSet:
        return "map_set";
    case IntrinsicId::MapHas:
        return "map_has";
    case IntrinsicId::MapRemove:
        return "map_remove";
    case IntrinsicId::MapClear:
        return "map_clear";
    case IntrinsicId::MapIterBegin:
        return "map_iter_begin";
    case IntrinsicId::MapIterNext:
        return "map_iter_next";
    case IntrinsicId::MapIterEnd:
        return "map_iter_end";
    case IntrinsicId::StringLen:
        return "string_len";
    case IntrinsicId::StringSubstr:
        return "string_substr";
    case IntrinsicId::StringCharAt:
        return "string_char_at";
    case IntrinsicId::StringFind:
        return "string_find";
    case IntrinsicId::StringContains:
        return "string_contains";
    case IntrinsicId::StringStartsWith:
        return "string_starts_with";
    case IntrinsicId::StringEndsWith:
        return "string_ends_with";
    case IntrinsicId::StringToUpper:
        return "string_to_upper";
    case IntrinsicId::StringToLower:
        return "string_to_lower";
    case IntrinsicId::StringTrim:
        return "string_trim";
    case IntrinsicId::StringReplace:
        return "string_replace";
    case IntrinsicId::StringSplit:
        return "string_split";
    case IntrinsicId::StringJoin:
        return "string_join";
    case IntrinsicId::StringToInt:
        return "string_to_int";
    case IntrinsicId::StringToFloat:
        return "string_to_float";
    case IntrinsicId::StringFromCharCode:
        return "string_from_char_code";
    case IntrinsicId::StringConcat:
        return "string_concat";
    case IntrinsicId::StringAppend:
        return "string_append";
    case IntrinsicId::StringInsert:
        return "string_insert";
    case IntrinsicId::StringReverse:
        return "string_reverse";
    default:
        return "<unknown>";
    }
}

} // namespace nw::smalls
