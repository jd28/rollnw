#pragma once

#include "../kernel/Strings.hpp"
#include "../util/Variant.hpp"
#include "Alignment.hpp"

#include <absl/container/inlined_vector.h>
#include <flecs/flecs.h>

#include <limits>
#include <optional>
#include <string>
#include <variant>

namespace nw {

using RuleValue = Variant<int32_t, float, std::string>;

// == Selector ================================================================

/// Selector types
enum struct SelectorType : uint32_t {
    ability,       ///< Subtype: ability_* constant
    ac,            ///< Subtype: ac_* constant
    alignment,     ///< Subtype: AlignmentAxis
    arcane_level,  ///< Subtype: none
    bab,           ///< Subtype: none
    caster_level,  ///< Subtype:
    class_level,   ///< Subtype: class_* constant
    feat,          ///< Subtype: feat_* constant
    level,         ///< Subtype: none
    local_var_int, ///< Subtype: local var name, eg. "X1_AllowArcher"
    local_var_str, ///< Subtype: local var name, eg. "some_var"
    race,          ///< Subtype: none
    skill,         ///< Subtype: skill_* constant
    spell_level,   ///< Subtype:
};

struct Selector {
    SelectorType type;
    RuleValue subtype;
};

bool operator==(const Selector& lhs, const Selector& rhs);
bool operator<(const Selector& lhs, const Selector& rhs);

// == Qualifier ===============================================================

struct Qualifier {
    Selector selector;
    absl::InlinedVector<RuleValue, 4> params;
};

// == Requirement =============================================================

struct Requirement {
    explicit Requirement(bool conjunction_ = true);
    explicit Requirement(std::initializer_list<Qualifier> quals, bool conjunction_ = true);
    void add(Qualifier qualifier);

    absl::InlinedVector<Qualifier, 8> qualifiers;
    bool conjunction = true;
};

// == Versus ==================================================================

struct Versus {
    uint64_t race_flags = 0;
    AlignmentFlags align_flags = AlignmentFlags::none;
    AlignmentAxis align_axis = AlignmentAxis::neither;
    bool trap = false;
};

// == Modifier ================================================================

enum struct ModifierSource {
    unknown,
    ability,
    class_,
    environment,
    feat,
    race,
    situation,
    skill,
};

using ModifierResult = Variant<int, float>;
using ModifierFunction = std::function<ModifierResult(flecs::entity)>;
using ModifierVariant = Variant<int, float, ModifierFunction>;

struct Modifier {
    int type = -1;
    ModifierVariant value;
    InternedString tagged;
    ModifierSource source = ModifierSource::unknown;
    Requirement requirement = Requirement{};
    Versus versus = {};
    int subtype = -1;
};

inline bool operator<(const Modifier& lhs, const Modifier& rhs)
{
    return std::tie(lhs.type, lhs.type, lhs.source) < std::tie(rhs.type, rhs.type, rhs.source);
}

} // namespace nw
