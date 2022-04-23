#pragma once

#include "../kernel/Strings.hpp"

#include <absl/container/inlined_vector.h>

#include <optional>
#include <string>
#include <variant>

namespace nw {

// Forward Decls
struct ObjectBase;
struct Creature;

using RuleVariant = std::variant<int, float, std::string>;

// == Constant ================================================================

struct Constant {
    kernel::InternedString name;
    RuleVariant value;

    /// Convenience function for using a constant as an index into a vector/array.
    size_t as_index() const;

    /// Gets integer value
    int as_int() const;

    /// Gets floating point value
    float as_float() const;

    /// Gets string value
    const std::string& as_string() const;

    /// Determines if variant is holding an int
    bool is_int() const noexcept;

    /// Determines if variant is holding an float
    bool is_float() const noexcept;

    /// Determines if variant is holding a std::string
    bool is_string() const noexcept;
};

bool operator==(const Constant& lhs, const Constant& rhs);

} // namespace nw
