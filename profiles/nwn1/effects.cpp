#include "effects.hpp"

#include "constants.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/rules/Ability.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/ItemProperty.hpp>

#include <limits>

namespace nwn1 {

bool effect_apply_is_creature(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj->as_creature();
}

bool effect_remove_is_creature(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj->as_creature();
}

nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? effect_type_ability_increase : effect_type_ability_decrease;
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(type);
    eff->subtype = *ability;
    eff->set_int(0, value);
    return eff;
}

nw::Effect* effect_haste()
{
    return nw::kernel::effects().create(effect_type_haste);
}

bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*)
{
    if (auto cre = obj->as_creature()) {
        ++cre->hasted;
        return true;
    }
    return false;
};

bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*)
{
    if (auto cre = obj->as_creature()) {
        if (cre->hasted) { --cre->hasted; }
        return true;
    }
    return false;
};

// -- Item Property Generators ------------------------------------------------
// ----------------------------------------------------------------------------

nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_ability_bonus : *ip_decreased_ability_score;
    result.subtype = uint16_t(*ability);
    result.cost_value = uint8_t(modifier);
    return result;
}

nw::Effect* ip_gen_ability_modifier(const nw::ItemProperty& ip)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto abil = nw::Ability::make(ip.subtype);
    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_ability_bonus || type == ip_decreased_ability_score) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_ability_modifier(abil, *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_haste()
{
    nw::ItemProperty result;
    result.type = *ip_haste;
    return result;
}

nw::Effect* ip_gen_haste(const nw::ItemProperty&)
{
    return effect_haste();
}

} // namespace nwn1