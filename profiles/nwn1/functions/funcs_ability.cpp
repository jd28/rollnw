#include "funcs_ability.hpp"

#include "../constants.hpp"
#include "../functions.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/EffectArray.hpp>
#include <nw/components/ObjectHandle.hpp>
#include <nw/functions.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/rules/attributes.hpp>

#include <algorithm>

namespace nwn1 {

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base)
{
    if (!obj || ability == nw::Ability::invalid()) { return 0; }

    // Base
    int result = obj->stats.get_ability_score(ability);

    // Race
    auto race = nw::kernel::rules().races.get(obj->race);
    if (race) { result += race->ability_modifiers[ability.idx()]; }

    // Modifiers = Feats, etc
    nw::kernel::resolve_modifier(obj, mod_type_ability, ability,
        [&result](int value) { result += value; });

    if (base) { return result; }

    // Effects
    auto end = std::end(obj->effects());
    int value = 0;
    auto callback = [&value](int result) { value += result; };

    auto it = nw::find_first_effect_of(std::begin(obj->effects()), end,
        effect_type_ability_increase, *ability);
    it = nw::resolve_effects_of<int>(it, end, effect_type_ability_increase, *ability, {},
        callback, &nw::effect_extract_int0);

    int bonus = value;
    value = 0;

    it = nw::find_first_effect_of(it, end, effect_type_ability_decrease, *ability);
    nw::resolve_effects_of<int>(it, end, effect_type_ability_decrease, *ability, {},
        callback, &nw::effect_extract_int0);
    int decrease = value;

    auto [min, max] = nw::kernel::rules().ability_effect_limits();
    return result + std::clamp(bonus - decrease, min, max);
}

int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base)
{
    return (get_ability_score(obj, ability, base) - 10) / 2;
}

int get_dex_modifier(const nw::Creature* obj)
{
    int base = get_ability_modifier(obj, ability_dexterity);
    auto item = get_equipped_item(obj, nw::EquipIndex::chest);
    if (item && item->baseitem == base_item_armor && item->armor_id != -1) {
        // [TODO] Optimize
        auto& tda = nw::kernel::twodas();
        if (auto armor = tda.get("armor")) {
            if (auto result = armor->get<int32_t>(item->armor_id, "DEXBONUS")) {
                return std::min(base, *result);
            }
        }
    }
    return base;
}

} // namespace nwn1
