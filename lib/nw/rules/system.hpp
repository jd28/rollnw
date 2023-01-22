#pragma once

#include "../kernel/Strings.hpp"
#include "../objects/ObjectBase.hpp"
#include "../util/Variant.hpp"
#include "Versus.hpp"
#include "attributes.hpp"
#include "combat.hpp"
#include "rule_type.hpp"
#include "system.hpp"

#include <absl/container/inlined_vector.h>

#include <limits>
#include <optional>
#include <string>
#include <variant>

namespace nw {

struct ObjectBase;

using RuleValue = Variant<int32_t, float, std::string>;

// == Selector ================================================================
// ============================================================================

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
    hitpoints_max, ///< Subtype: none
    level,         ///< Subtype: none
    local_var_int, ///< Subtype: local var name, eg. "X1_AllowArcher"
    local_var_str, ///< Subtype: local var name, eg. "some_var"
    race,          ///< Subtype: none
    skill,         ///< Subtype: skill_* constant
    spell_level,   ///< Subtype:
};

struct Selector {
    SelectorType type;
    RuleValue subtype{};
};

// == Qualifier ===============================================================
// ============================================================================

struct Qualifier {
    Selector selector;
    absl::InlinedVector<RuleValue, 4> params;
};

// == Requirement =============================================================
// ============================================================================

struct Requirement {
    explicit Requirement(bool conjunction_ = true);
    explicit Requirement(std::initializer_list<Qualifier> quals, bool conjunction_ = true);
    void add(Qualifier qualifier);
    size_t size() const noexcept;

    absl::InlinedVector<Qualifier, 8> qualifiers;
    bool conjunction = true;
};

// == Modifier ================================================================
// ============================================================================

DECLARE_RULE_TYPE(ModifierType);

enum struct ModifierSource {
    unknown,
    ability,
    class_,
    combat_mode,
    environment,
    feat,
    race,
    situation,
    skill,
};

using ModifierResult = Variant<int, float, DamageRoll>;
using ModifierFunction = std::function<ModifierResult(const ObjectBase*)>;
using ModifierSubFunction = std::function<ModifierResult(const ObjectBase*, int32_t)>;
using ModifierVsFunction = std::function<ModifierResult(const ObjectBase*, const ObjectBase*)>;
using ModifierSubVsFunction = std::function<ModifierResult(const ObjectBase*, const ObjectBase*, int32_t)>;

using ModifierVariant = Variant<
    int,
    float,
    DamageRoll,
    ModifierFunction,
    ModifierSubFunction,
    ModifierVsFunction,
    ModifierSubVsFunction>;

struct Modifier {
    ModifierType type = ModifierType::invalid();
    ModifierVariant input;
    InternedString tagged;
    ModifierSource source = ModifierSource::unknown;
    Requirement requirement = Requirement{};
    Versus versus = {};
    int subtype = -1;
};

inline bool operator<(const Modifier& lhs, const Modifier& rhs)
{
    return std::tie(lhs.type, lhs.subtype, lhs.source) < std::tie(rhs.type, rhs.subtype, rhs.source);
}

struct ModifierRegistry {
    using Storage = std::vector<Modifier>;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;

    /// Adds a modifier to the system
    void add(Modifier mod);

    iterator begin();
    const_iterator begin() const;
    const_iterator cbegin() const;

    /// Clears all modifiers
    void clear();

    iterator end();
    const_iterator end() const;
    const_iterator cend() const;

    /**
     * @brief Removes modifiers by tag
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @return int number of modifiers affected
     */
    int remove(std::string_view tag);

    /**
     * @brief Replace modifier value
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @param value new value
     * @return int number of modifiers affected
     */
    int replace(std::string_view tag, ModifierVariant value);

    /**
     * @brief Replace modifier requirement
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @param req new requirement
     * @return int number of modifiers affected
     */
    int replace(std::string_view tag, const Requirement& req);

    /// Gets the number of modifiers
    size_t size() const;

private:
    Storage entries_;
};

} // namespace nw
