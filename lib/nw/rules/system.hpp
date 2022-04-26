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

    operator bool() const noexcept { return !!name; }
};

bool operator==(const Constant& lhs, const Constant& rhs);
bool operator<(const Constant& lhs, const Constant& rhs);

// == Selector ================================================================

enum struct SelectorType {
    race,
    ability,
    level,
    class_level,
    feat,
    skill,
    bab,
    ac,
};

struct Selector {
    SelectorType type;
    Constant subtype;

    std::optional<int> select(const Creature& cre) const;
};

bool operator==(const Selector& lhs, const Selector& rhs);
bool operator<(const Selector& lhs, const Selector& rhs);

// Convenience functions
namespace select {

Selector ability(Constant id);
// Selector armor_class(int id, bool base = false);
// Selector class_level(int id);
// Selector feat(int id);
Selector level();
Selector skill(Constant id);
Selector race();

} // namespace select

// == Qualifier ===============================================================

struct Qualifier {
    Selector selector;
    absl::InlinedVector<int, 4> params;

    bool match(const Creature& cre) const;
};

namespace qualifier {

Qualifier ability(Constant id, int min, int max = 0);
Qualifier level(int min, int max = 0);
Qualifier race(Constant id);
Qualifier skill(Constant id, int min, int max = 0);

} // namespace qualifier

// == Requirement =============================================================

struct Requirement {
    Requirement() = default;
    Requirement(std::initializer_list<Qualifier> quals);
    void add(Qualifier qualifier);
    bool met(const Creature& cre) const;

private:
    absl::InlinedVector<Qualifier, 8> qualifiers_;
};

} // namespace nw
