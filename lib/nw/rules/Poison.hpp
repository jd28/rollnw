#pragma once

#include "type_traits.hpp"

#include <cstdint>
#include <limits>

namespace nw {

enum struct Poison : uint32_t {
    invalid = std::numeric_limits<uint32_t>::max(),
};
constexpr Poison make_poison(uint32_t index) { return static_cast<Poison>(index); }

template <>
struct is_rule_type_base<Poison> : std::true_type {
};

} // namespace nw
