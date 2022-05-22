#pragma once

#include "../kernel/Strings.hpp"
#include "../util/Variant.hpp"

// == Constant ================================================================

namespace nw {

/**
 * @brief A `Constant` is an InternedString and an `int`, `float`, or `std::string` value.
 *
 * @code {.cpp}
 * Constant c = ...;
 * if(c->is<int32_t>()) {
 *     int v = c->as<int32_t>();
 * }
 * @endcode
 *
 */
struct Constant {
    using variant_type = Variant<int32_t, float, std::string>;

    Constant() = default;
    Constant(kernel::InternedString name_, variant_type value_)
        : name{name_}
        , value{std::move(value_)}
    {
    }

    kernel::InternedString name;
    variant_type value;

    variant_type* operator->() noexcept { return &value; }
    const variant_type* operator->() const noexcept { return &value; }
    bool empty() const noexcept { return !name; }
};

bool operator==(const Constant& lhs, const Constant& rhs);
bool operator<(const Constant& lhs, const Constant& rhs);

} // namespace nw
