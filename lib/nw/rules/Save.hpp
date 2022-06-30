#pragma once

#include "type_traits.hpp"

#include <cstdint>
#include <limits>

namespace nw {

enum struct Save : int32_t {
    invalid = -1,
};
constexpr Save make_save(int32_t id) { return static_cast<Save>(id); }

template <>
struct is_rule_type_base<Save> : std::true_type {
};

struct SaveInfo {
};

} // namespace nw
