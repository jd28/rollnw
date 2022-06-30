#pragma once

#include <type_traits>

namespace nw {

template <typename T>
struct is_rule_type_base : std::false_type {
};

template <typename T>
struct is_rule_type : is_rule_type_base<std::remove_cv_t<T>> {
};

} // namespace nw
