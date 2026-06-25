#pragma once

#include <compare>
#include <cstdint>
#include <limits>

namespace nw::render {

struct ModelInstanceHandle {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    bool operator==(const ModelInstanceHandle&) const = default;
    auto operator<=>(const ModelInstanceHandle&) const = default;

    [[nodiscard]] bool valid() const noexcept { return generation != 0 && index != std::numeric_limits<uint32_t>::max(); }
};

inline constexpr uint32_t kInvalidModelInstanceIndex = std::numeric_limits<uint32_t>::max();

} // namespace nw::render
