#pragma once

#include "../kernel/Strings.hpp"
#include "../util/Variant.hpp"
#include "Alignment.hpp"
#include "Constant.hpp"

#include <absl/container/inlined_vector.h>
#include <flecs/flecs.h>

#include <optional>
#include <string>
#include <variant>

namespace nw {

using RuleValue = Variant<int32_t, float, std::string>;

// == Selector ================================================================

enum struct SelectorType {
    ability,     ///< Subtype: ABILITY_* constant
    ac,          ///< Subtype: AC_* constant
    alignment,   ///< Subtype: AlignmentAxis
    bab,         ///< Subtype: none
    class_level, ///< Subtype: CLASS_* constant
    feat,        ///< Subtype: FEAT_* constant
    level,       ///< Subtype: none
    race,        ///< Subtype: none
    skill,       ///< Subtype: SKILL_* constant
};

struct Selector {
    SelectorType type;
    int subtype;
};

bool operator==(const Selector& lhs, const Selector& rhs);
bool operator<(const Selector& lhs, const Selector& rhs);

// Convenience functions
namespace select {

Selector ability(Constant id);
// Selector armor_class(int id, bool base = false);
// Selector class_level(int id);
Selector alignment(AlignmentAxis id);
Selector feat(Constant id);
Selector level();
Selector skill(Constant id);
Selector race();

} // namespace select

// == Qualifier ===============================================================

struct Qualifier {
    Selector selector;
    absl::InlinedVector<RuleValue, 4> params;

    bool match(const flecs::entity cre) const;
};

namespace qualifier {

Qualifier ability(Constant id, int min, int max = 0);
Qualifier alignment(AlignmentAxis axis, AlignmentFlags flags);
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
    bool met(const flecs::entity cre) const;

private:
    absl::InlinedVector<Qualifier, 8> qualifiers_;
    bool conjunction_ = true;
};

} // namespace nw
