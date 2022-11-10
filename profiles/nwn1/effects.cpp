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

nw::Effect* ip_gen_haste(const nw::ItemProperty& ip)
{
    return effect_haste();
}

} // namespace nwn1
