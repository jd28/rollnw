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

enum struct DamageModType : int32_t {
    invalid = -1,
};

constexpr DamageModType make_damage_mod(int id) { return static_cast<DamageModType>(id); }

template <>
struct is_rule_type_base<DamageModType> : std::true_type {
};

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

struct DamageModifier {
    DamageModType type = DamageModType::invalid;
    DamageFlag damage_type;
    int32_t amount = 0;
    int32_t remaining = -1;
};

} // namespace nw
