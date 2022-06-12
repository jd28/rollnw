#pragma once

#include "templates.hpp"

/// Defines bitwise functions for an ``enum`` type
#define DEFINE_ENUM_FLAGS(T)                                                                        \
    constexpr T operator&(T x, T y) { return static_cast<T>(to_underlying(x) & to_underlying(y)); } \
    constexpr T operator|(T x, T y) { return static_cast<T>(to_underlying(x) | to_underlying(y)); } \
    constexpr T operator^(T x, T y) { return static_cast<T>(to_underlying(x) ^ to_underlying(y)); } \
    constexpr T operator~(T x) { return static_cast<T>(~to_underlying(x)); }                        \
    inline T& operator&=(T& x, T y) { return x = (x & y); }                                         \
    inline T& operator|=(T& x, T y) { return x = (x | y); }                                         \
    inline T& operator^=(T& x, T y) { return x = (x ^ y); }                                         \
    constexpr bool operator!(T x) { return !to_underlying(x); }

/// Converts enum flag to boolean
template <typename T>
constexpr bool to_bool(const T thing)
{
    static_assert(std::is_enum_v<T>, "to_bool is provided only for enum flags");
    return !!thing;
}
