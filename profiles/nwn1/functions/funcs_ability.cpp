#include "../constants.hpp"
#include "nw/components/ObjectHandle.hpp"
#include "nw/rules/Effect.hpp"
#include "nw/rules/Spell.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/Ability.hpp>

#include <algorithm>

namespace nwn1 {

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false)
{
    if (!obj || ability == nw::Ability::invalid()) { return 0; }

    // Base
    int result = obj->stats.get_ability_score(ability);

    // Race
    auto race = nw::kernel::rules().races.get(obj->race);
    if (race) { result += race->ability_modifiers[ability.idx()]; }

    // Modifiers = Feats, etc
    nw::kernel::rules().calculate(obj, mod_type_ability, ability,
        [&result](int value) { result += value; });

    if (base) { return result; }

    // Effects
    auto comp = [](const nw::EffectHandle& handle, nw::EffectType type) {
        return *handle.type < *type;
    };

    auto calculate = [](auto it, auto end, nw::EffectType type, nw::Ability subtype) -> int {
        int result = 0;
        while (it != end && it->type == type && it->subtype == *subtype) {
            if (it->creator.type == nw::ObjectType::item) {
                auto item = it->creator;
                int value = it->effect->get_int(0);
                while (it != end && it->type == type
                    && it->subtype == *subtype
                    && it->creator == item) {
                    value = std::max(value, it->effect->get_int(0));
                    ++it;
                }
                result += value;
            } else if (it->spell_id != nw::Spell::invalid()) {
                auto spell = it->spell_id;
                int value = it->effect->get_int(0);
                while (it != end && it->type == type
                    && it->subtype == *subtype
                    && it->spell_id == spell) {
                    value = std::max(value, it->effect->get_int(0));
                    ++it;
                }
                result += value;
            } else {
                result += it->effect->get_int(0);
                ++it;
            }
        }
        return result;
    };

    auto end = std::end(obj->effects());
    auto it = std::lower_bound(std::begin(obj->effects()), end,
        effect_type_ability_increase, comp);
    int bonus = calculate(it, end, effect_type_ability_increase, ability);

    it = std::lower_bound(it, end, effect_type_ability_decrease, comp);
    int decrease = calculate(it, end, effect_type_ability_decrease, ability);

    LOG_F(INFO, "{}, {}", bonus, decrease);

    return result + std::clamp(bonus, 0, 12) - std::clamp(decrease, 0, 12);
}

int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false)
{
    return (get_ability_score(obj, ability, base) - 10) / 2;
}

} // namespace nwn1
