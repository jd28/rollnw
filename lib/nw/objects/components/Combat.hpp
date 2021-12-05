#pragma once

#include "../../serialization/Serialization.hpp"
#include "SpellBook.hpp"

namespace nw {

struct SpecialAbility {
    uint16_t spell;
    uint8_t level;
    SpellFlags flags = SpellFlags::none;
};

struct Combat {
    Combat(const GffStruct gff);

    uint8_t ac_natural = 0;
    std::vector<SpecialAbility> special_abilities;
};

} // namespace nw
