#pragma once

#include "../util/FunctionPtr.hpp"
#include "../util/enum_flags.hpp"
#include "Effect.hpp"
#include "damage.hpp"
#include "rule_type.hpp"

#include <absl/container/inlined_vector.h>

#include <cstdint>

namespace nw {

struct Creature;
struct Item;
struct ObjectBase;
struct ModifierResult;
struct ModifierType;

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

// -- Disease -----------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Disease);

// -- MissChance --------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(MissChanceType);

// -- Modes -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(CombatMode);

struct CombatModeFuncs {
    FunctionPtr<ModifierResult(CombatMode, ModifierType, const Creature*)> modifier;
    FunctionPtr<bool(CombatMode, const Creature*)> can_use;
    FunctionPtr<void(CombatMode, Creature*)> apply;
    FunctionPtr<void(CombatMode, Creature*)> remove;
};

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

struct SpecialAttackFuncs {
    FunctionPtr<ModifierResult(SpecialAttack, ModifierType, Creature*, const ObjectBase*)> modifier;
    FunctionPtr<bool(SpecialAttack, Creature*, const ObjectBase*)> usable;
    FunctionPtr<Effect*(SpecialAttack, Creature*, const ObjectBase*)> imapct;
};

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

struct AttackFuncs {
    FunctionPtr<std::unique_ptr<nw::AttackData>(nw::Creature* attacker, nw::ObjectBase* target)> resolve_attack;
    FunctionPtr<int(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* versus)> resolve_attack_bonus;
    FunctionPtr<int(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data)> resolve_attack_damage;
    FunctionPtr<nw::AttackResult(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs, nw::AttackData* data)> resolve_attack_roll;
    FunctionPtr<nw::AttackType(const nw::Creature* obj)> resolve_attack_type;
    FunctionPtr<std::pair<int, bool>(const nw::ObjectBase* obj, const nw::ObjectBase* target, bool vs_ranged)> resolve_concealment;
    FunctionPtr<int(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs)> resolve_critical_multiplier;
    FunctionPtr<int(const nw::Creature* obj, nw::AttackType type)> resolve_critical_threat;
    FunctionPtr<void(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data)> resolve_damage_modifiers;
    FunctionPtr<int(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus)> resolve_damage_immunity;
    FunctionPtr<std::pair<int, nw::Effect*>(const nw::ObjectBase* obj, int power, const nw::ObjectBase* versus)> resolve_damage_reduction;
    FunctionPtr<std::pair<int, nw::Effect*>(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus)> resolve_damage_resistance;
    FunctionPtr<std::pair<int, int>(const nw::Creature* obj)> resolve_dual_wield_penalty;
    FunctionPtr<int(const nw::Creature* attacker, nw::AttackType type)> resolve_iteration_penalty;
    FunctionPtr<std::pair<int, int>(const nw::Creature* obj)> resolve_number_of_attacks;
    FunctionPtr<nw::TargetState(const nw::Creature* attacker, const nw::ObjectBase* target)> resolve_target_state;
    FunctionPtr<nw::DamageFlag(const nw::Item* weapon)> resolve_weapon_damage_flags;
    FunctionPtr<int(const nw::Creature* obj, const nw::Item* weapon)> resolve_weapon_power;
};

} // namespace nw
