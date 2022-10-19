#include "../constants.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/Ability.hpp>

namespace nwn1 {

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false)
{
    if (!obj || ability == nw::Ability::invalid()) {
        return 0;
    }

    // Base
    int result = obj->stats.abilities[*ability];
    nw::kernel::rules().calculate(obj, mod_type_ability, ability,
        [&result](int value) { result += value; });

    if (base) {
        return result;
    }

    // Effects

    return result;
}

int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false)
{
    return (get_ability_score(obj, ability, base) - 10) / 2;
}

} // namespace nwn1
