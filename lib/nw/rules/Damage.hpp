#pragma once

#include "../util/enum_flags.hpp"
#include "Dice.hpp"

namespace nw {

enum struct DamageFlags {
    none = 0,
    penalty = 1 << 0,
    critical = 1 << 1,
    unblockable = 1 << 2,
};

DEFINE_ENUM_FLAGS(DamageFlags);

struct DamageRoll {
    int type;
    DiceRoll roll;
    DamageFlags flags = DamageFlags::none;
};

} // namespace nw
