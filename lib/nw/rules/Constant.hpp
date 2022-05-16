#pragma once

#include "../kernel/Strings.hpp"
#include "../util/Variant.hpp"

// == Constant ================================================================

namespace nw {

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

// == ConstantRegistry ========================================================

struct ConstantRegistry {
    using container_type = absl::flat_hash_map<
        kernel::InternedString,
        Constant::variant_type,
        kernel::InternedStringHash,
        kernel::InternedStringEq>;

    /// Adds constant
    Constant add(std::string_view name, Constant::variant_type value);

    /// Clears all constants
    void clear();

    /// Gets constant
    Constant get(std::string_view name) const;

private:
    container_type constants_;
};

} // namespace nw
