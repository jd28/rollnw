#pragma once

#include "../util/enum_flags.hpp"
#include "Dice.hpp"
#include "RuleFlag.hpp"
#include "type_traits.hpp"

namespace nw {

enum struct Damage : int32_t {
    invalid = -1,
};

constexpr Damage make_damage(int id) { return static_cast<Damage>(id); }

template <>
struct is_rule_type_base<Damage> : std::true_type {
};

using DamageFlag = RuleFlag<Damage, 64>;

enum struct DamageCategory : uint64_t {
    none = 0,
    penalty = 1 << 0,
    critical = 1 << 1,
    unblockable = 1 << 2,
};

DEFINE_ENUM_FLAGS(DamageCategory);

struct DamageRoll {
    DamageFlag type;
    DiceRoll roll;
    DamageCategory flags = DamageCategory::none;
};

} // namespace nw
