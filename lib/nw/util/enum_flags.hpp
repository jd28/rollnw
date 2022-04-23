#pragma once

#include "templates.hpp"

#define DEFINE_ENUM_FLAGS(T)                                                                     \
    inline T operator&(T x, T y) { return static_cast<T>(to_underlying(x) & to_underlying(y)); } \
    inline T operator|(T x, T y) { return static_cast<T>(to_underlying(x) | to_underlying(y)); } \
    inline T operator^(T x, T y) { return static_cast<T>(to_underlying(x) ^ to_underlying(y)); } \
    inline T operator~(T x) { return static_cast<T>(~to_underlying(x)); }                        \
    inline T& operator&=(T& x, T y) { return x = (x & y); }                                      \
    inline T& operator|=(T& x, T y) { return x = (x | y); }                                      \
    inline T& operator^=(T& x, T y) { return x = (x ^ y); }                                      \
    inline bool operator!(T x) { return !to_underlying(x); }
