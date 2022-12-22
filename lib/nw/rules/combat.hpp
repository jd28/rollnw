#pragma once

#include "../util/enum_flags.hpp"
#include "Dice.hpp"
#include "rule_type.hpp"

#include <cstdint>

namespace nw {

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

enum struct DamageCategory : uint64_t {
    none = 0,
    penalty = 1 << 0,
    critical = 1 << 1,
    unblockable = 1 << 2,
};

DEFINE_ENUM_FLAGS(DamageCategory)

struct DamageRoll {
    DamageFlag type;
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

DEFINE_ENUM_FLAGS(TargetState);

} // namespace nw
