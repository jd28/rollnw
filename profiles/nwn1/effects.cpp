#include "effects.hpp"

#include "constants.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/ItemProperty.hpp>

namespace nwn1 {

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

nw::ItemProperty itemprop_ability_bonus(nw::Ability ability, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = *ip_ability_bonus;
    result.subtype = uint16_t(*ability);
    result.cost_value = uint8_t(modifier);
    return result;
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
