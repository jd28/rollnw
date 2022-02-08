#pragma once

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

} // namespace nw
