#pragma once

#include "type_traits.hpp"

#include <cstdint>

namespace nw {

enum struct AttackType : int32_t {
    invalid = -1
};
constexpr AttackType make_attack_type(int32_t id) { return static_cast<AttackType>(id); }

template <>
struct is_rule_type_base<AttackType> : std::true_type {
};

} // namespace nw
