#pragma once

#include "../util/enum_flags.hpp"
#include "type_traits.hpp"

#include <limits>

namespace nw {

enum struct Situation : int32_t {
    invalid = -1,
};

constexpr Situation make_situation(int id) { return static_cast<Situation>(id); }

template <>
struct is_rule_type_base<Situation> : std::true_type {
};

struct SituationInfo {
};

} // namespace nw
