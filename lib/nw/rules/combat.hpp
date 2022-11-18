#pragma once

#include "../util/enum_flags.hpp"
#include "Dice.hpp"
#include "rule_type.hpp"

#include <cstdint>

namespace nw {

// -- Attack ------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(AttackType);

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
