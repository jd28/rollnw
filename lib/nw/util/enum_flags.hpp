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
