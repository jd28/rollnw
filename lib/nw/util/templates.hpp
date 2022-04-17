#pragma once

#include <fstream>

namespace nw {

template <typename T>
constexpr bool always_false() { return false; }

// Replace when C++23 comes around
template <class Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept
{
    static_assert(std::is_enum_v<Enum>, "to_underlying must be passed an enum");
    return static_cast<std::underlying_type_t<Enum>>(e);
}

// From https://stackoverflow.com/questions/8542591/c11-reverse-range-based-for-loop
template <typename T>
struct reversion_wrapper {
    T& iterable;
};

template <typename T>
auto begin(reversion_wrapper<T> w) { return std::rbegin(w.iterable); }

template <typename T>
auto end(reversion_wrapper<T> w) { return std::rend(w.iterable); }

template <typename T>
reversion_wrapper<T> reverse(T&& iterable) { return {iterable}; }

template <typename T, typename U>
std::ostream& ostream_write(std::ostream& stream, const T* data, U size)
{
    static_assert(std::is_integral_v<U>, "size must be an integral type");
    return stream.write(reinterpret_cast<const char*>(data),
        static_cast<std::streamsize>(size));
}

template <typename T, typename U>
std::istream& istream_read(std::istream& stream, T* data, U size)
{
    static_assert(std::is_integral_v<U>, "size must be an integral type");
    return stream.read(reinterpret_cast<char*>(data),
        static_cast<std::streamsize>(size));
}

} // namespace nw
