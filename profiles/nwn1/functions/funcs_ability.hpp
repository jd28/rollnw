#pragma once

#include "../constants.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/rules/Ability.hpp>
#include <nw/util/EntityView.hpp>

namespace nwn1 {

inline int get_ability_score(nw::EntityView<nw::CreatureStats> ent,
    nw::Ability ability, bool base = false)
{
    if (!ent || ability == nw::Ability::invalid) {
        return 0;
    }
    auto stats = ent.get<nw::CreatureStats>();

    // Base
    int result = stats->abilities[static_cast<size_t>(ability)];
    result += nw::kernel::rules().calculate<int>(ent.ent, mod_type_ability, ability);

    if (base) {
        return result;
    }

    // Effects

    return result;
}

} // namespace nwn1
