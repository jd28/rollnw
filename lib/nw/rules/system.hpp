#pragma once

#include "../kernel/Strings.hpp"
#include "../util/Variant.hpp"

#include <absl/container/inlined_vector.h>

#include <optional>
#include <string>
#include <variant>

namespace nw {

// Forward Decls
struct ObjectBase;
struct Creature;

using RuleBaseVariant = Variant<int32_t, float, std::string>;

// == Constant ================================================================

struct Constant {
    Constant() = default;
    Constant(kernel::InternedString name_, RuleBaseVariant value_)
        : name{name_}
        , value{std::move(value_)}
    {
    }

    kernel::InternedString name;
    RuleBaseVariant value;

    RuleBaseVariant* operator->() noexcept { return &value; }
    const RuleBaseVariant* operator->() const noexcept { return &value; }
    bool empty() const noexcept { return !name; }
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
    int subtype;

    RuleBaseVariant select(const Creature& cre) const;
};

bool operator==(const Selector& lhs, const Selector& rhs);
bool operator<(const Selector& lhs, const Selector& rhs);

// Convenience functions
namespace select {

Selector ability(Constant id);
// Selector armor_class(int id, bool base = false);
// Selector class_level(int id);
Selector feat(Constant id);
Selector level();
Selector skill(Constant id);
Selector race();

} // namespace select

// == Qualifier ===============================================================

struct Qualifier {
    Selector selector;
    absl::InlinedVector<RuleBaseVariant, 4> params;

    bool match(const Creature& cre) const;
};

namespace qualifier {

Qualifier ability(Constant id, int min, int max = 0);
Qualifier level(int min, int max = 0);
Qualifier feat(Constant id);
Qualifier race(Constant id);
Qualifier skill(Constant id, int min, int max = 0);

} // namespace qualifier

// == Requirement =============================================================

struct Requirement {
    explicit Requirement(bool conjunction = true);
    explicit Requirement(std::initializer_list<Qualifier> quals, bool conjunction = true);
    void add(Qualifier qualifier);
    bool met(const Creature& cre) const;

private:
    absl::InlinedVector<Qualifier, 8> qualifiers_;
    bool conjunction_ = true;
};

} // namespace nw
