#pragma once

#include "type_traits.hpp"

#include <cstdint>
#include <limits>

namespace nw {

enum struct ArmorClass : int32_t {
    invalid = -1,
};
constexpr ArmorClass make_armor_class(int32_t id) { return static_cast<ArmorClass>(id); }

template <>
struct is_rule_type_base<ArmorClass> : std::true_type {
};

} // namespace nw
