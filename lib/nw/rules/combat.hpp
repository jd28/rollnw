#pragma once

#include "../objects/ObjectHandle.hpp"
#include "../util/enum_flags.hpp"
#include "Dice.hpp"
#include "Effect.hpp"
#include "rule_type.hpp"

#include <absl/container/inlined_vector.h>

#include <cstdint>

namespace nw {

struct Creature;
struct Item;
struct ObjectBase;

// -- Attack ------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(AttackType);

enum struct AttackResult {
    hit_by_auto_success,
    hit_by_critical,
    hit_by_roll,
    miss_by_auto_fail,
    miss_by_concealment,
    miss_by_miss_chance,
    miss_by_roll,
};

constexpr bool is_attack_type_hit(AttackResult value)
{
    switch (value) {
    default:
        return false;
    case AttackResult::hit_by_auto_success:
        return true;
    case AttackResult::hit_by_critical:
        return true;
    case AttackResult::hit_by_roll:
        return true;
    case AttackResult::miss_by_auto_fail:
        return false;
    case AttackResult::miss_by_concealment:
        return false;
    case AttackResult::miss_by_miss_chance:
        return false;
    case AttackResult::miss_by_roll:
        return false;
    }
}

constexpr bool is_attack_type_miss(AttackResult value)
{
    switch (value) {
    default:
        return false;
    case AttackResult::hit_by_auto_success:
        return false;
    case AttackResult::hit_by_critical:
        return false;
    case AttackResult::hit_by_roll:
        return false;
    case AttackResult::miss_by_auto_fail:
        return true;
    case AttackResult::miss_by_concealment:
        return true;
    case AttackResult::miss_by_miss_chance:
        return true;
    case AttackResult::miss_by_roll:
        return true;
    }
}

// -- Damage ------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Damage);

using DamageFlag = RuleFlag<Damage, 32>;

DECLARE_RULE_TYPE(DamageModType);

enum struct DamageCategory : uint32_t {
    none = 0,
    penalty = 1 << 0,
    critical = 1 << 1,
    unblockable = 1 << 2,
};

DEFINE_ENUM_FLAGS(DamageCategory)

struct DamageRoll {
    Damage type = Damage::invalid();
    DiceRoll roll;
    DamageCategory flags = DamageCategory::none;
};

struct DamageModifier {
    DamageModType type = DamageModType::invalid();
    DamageFlag damage_type;
    int32_t amount = 0;
    int32_t remaining = -1;
};

// -- Disease -----------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Disease);

// -- MissChance --------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(MissChanceType);

// -- Modes -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(CombatMode);

// -- Poison ------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Poison);

// -- Situations --------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Situation);

using SituationFlag = RuleFlag<Situation, 64>;

// -- Special Attack ----------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(SpecialAttack);

// -- Target State ------------------------------------------------------------
// ----------------------------------------------------------------------------

enum struct TargetState {
    none = 0,
    blind = 1,
    attacker_invis = 2,
    unseen = 4,
    moving = 8,
    prone = 16,
    stunned = 32,
    flanked = 64,
    flatfooted = 128,
    asleep = 256,
    attacker_unseen = 512,
    invis = 1024,
};

DEFINE_ENUM_FLAGS(TargetState)

// -- Attack Data -------------------------------------------------------------
// ----------------------------------------------------------------------------

struct DamageResult {
    nw::Damage type = nw::Damage::invalid();
    int amount = 0;
    int unblocked = 0;
    int immunity = 0;
    int reduction = 0;
    int reduction_remaining = 0;
    int resist = 0;
    int resist_remaining = 0;
};

/// Structure for aggregating attack related data
struct AttackData {
    using DamageArray = absl::InlinedVector<DamageResult, 8>;

    Creature* attacker = nullptr;
    ObjectBase* target = nullptr;
    Item* weapon = nullptr;

    AttackType type = AttackType::invalid();
    AttackResult result = AttackResult::miss_by_roll;
    TargetState target_state = TargetState::none;

    bool target_is_creature = false;
    bool is_ranged_attack = false;
    bool is_killing_blow = false; ///< Is the attack enough to kill target
    int nth_attack = 0;           ///< The nth attack in the 'round'
    int attack_roll = 0;
    int attack_bonus = 0;
    int damage_total = 0;
    int armor_class = 0;
    int iteration_penalty = 0;
    int multiplier = 0;
    int threat_range = 0;
    int concealment = 0;

    DamageResult damage_base;                                   ///< Base weapon damage
    absl::InlinedVector<nw::Effect*, 8> effects_to_apply;       ///< Effects to apply to target
    absl::InlinedVector<nw::EffectHandle, 8> effects_to_remove; ///< Effects to remove from target

    /// Adds damage to damage result
    void add(nw::Damage type_, int amount, bool unblockable = false);

    /// Gets damage array
    DamageArray& damages();

    /// Gets damage array
    const DamageArray& damages() const;

private:
    DamageArray damage_;
};

} // namespace nw
