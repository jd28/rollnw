#pragma once

#include "../../rules/Constant.hpp"

namespace nw {

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
