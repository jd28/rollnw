#pragma once

#define DEFINE_ENUM_FLAGS(T)                                                                                                                       \
    inline T operator&(T x, T y) { return static_cast<T>(static_cast<std::underlying_type_t<T>>(x) & static_cast<std::underlying_type_t<T>>(y)); } \
    inline T operator|(T x, T y) { return static_cast<T>(static_cast<std::underlying_type_t<T>>(x) | static_cast<std::underlying_type_t<T>>(y)); } \
    inline T operator^(T x, T y) { return static_cast<T>(static_cast<std::underlying_type_t<T>>(x) ^ static_cast<std::underlying_type_t<T>>(y)); } \
    inline T operator~(T x) { return static_cast<T>(~static_cast<std::underlying_type_t<T>>(x)); }                                                 \
    inline T& operator&=(T& x, T y) { return x = (x & y); }                                                                                        \
    inline T& operator|=(T& x, T y) { return x = (x | y); }                                                                                        \
    inline T& operator^=(T& x, T y) { return x = (x ^ y); }                                                                                        \
    inline bool operator!(T x) { return !static_cast<std::underlying_type_t<T>>(x); }
