#pragma once

#include "type_traits.hpp"

#include <cstdint>
#include <limits>

namespace nw {

enum struct Disease : int32_t {
    invalid = -1,
};
constexpr Disease make_disease(int32_t index) { return static_cast<Disease>(index); }

template <>
struct is_rule_type_base<Disease> : std::true_type {
};

} // namespace nw
