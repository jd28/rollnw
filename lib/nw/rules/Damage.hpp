#pragma once

#include "../util/enum_flags.hpp"
#include "Dice.hpp"
#include "type_traits.hpp"

namespace nw {

enum struct Damage : int32_t {
    invalid = -1,
};

constexpr Damage make_damage(int id) { return static_cast<Damage>(id); }

template <>
struct is_rule_type_base<Damage> : std::true_type {
};

enum struct DamageFlags : uint64_t {
    none = 0,
    penalty = 1 << 0,
    critical = 1 << 1,
    unblockable = 1 << 2,
};

DEFINE_ENUM_FLAGS(DamageFlags);

struct DamageRoll {
    Damage type = Damage::invalid;
    DiceRoll roll;
    DamageFlags flags = DamageFlags::none;
};

} // namespace nw
