#include "../constants.hpp"
#include "nw/rules/Effect.hpp"

#include <algorithm>
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
    int result = obj->stats.get_ability_score(ability);
    nw::kernel::rules().calculate(obj, mod_type_ability, ability,
        [&result](int value) { result += value; });

    if (base) {
        return result;
    }

    // Effects - This is very naive and everything stacks..
    auto it = std::lower_bound(std::begin(obj->effects()), std::end(obj->effects()),
        effect_type_ability_increase,
        [](const nw::EffectHandle& handle, nw::EffectType type) {
            return *handle.type < *type;
        });

    while (it != std::end(obj->effects()) && it->type == effect_type_ability_increase) {
        result += it->effect->get_int(0);
        ++it;
    }

    // If creature has ability decrease effects they will be next in line.
    while (it != std::end(obj->effects()) && it->type == effect_type_ability_decrease) {
        result -= it->effect->get_int(0);
    }

    return result;
}

int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false)
{
    return (get_ability_score(obj, ability, base) - 10) / 2;
}

} // namespace nwn1
