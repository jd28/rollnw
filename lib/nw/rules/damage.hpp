#pragma once

#include "../util/enum_flags.hpp"
#include "Dice.hpp"
#include "rule_type.hpp"

namespace nw {

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

} // namespace nw
