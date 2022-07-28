#pragma once

#include "../constants.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/Ability.hpp>

namespace nwn1 {

inline int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false)
{
    if (!obj || ability == nw::Ability::invalid) {
        return 0;
    }

    // Base
    int result = obj->stats.abilities[static_cast<size_t>(ability)];
    result += nw::kernel::rules().calculate<int>(obj, mod_type_ability, ability);

    if (base) {
        return result;
    }

    // Effects

    return result;
}

} // namespace nwn1
