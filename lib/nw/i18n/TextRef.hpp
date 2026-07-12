#pragma once

#include "../config.hpp"

#include <compare>
#include <cstdint>
#include <utility>

namespace nw {

/// Small handle into the runtime text catalog.
struct TextRef {
    uint32_t value = 0;

    constexpr bool valid() const noexcept { return value != 0; }
    constexpr auto operator<=>(const TextRef&) const noexcept = default;

    template <typename H>
    friend H AbslHashValue(H h, const TextRef& ref)
    {
        return H::combine(std::move(h), ref.value);
    }
};

} // namespace nw
