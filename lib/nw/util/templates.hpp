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

} // namespace nw
