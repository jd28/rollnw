#include "effects.hpp"

#include <nw/components/Creature.hpp>
#include <nw/rules/Effect.hpp>

namespace nwn1 {

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

} // namespace nwn1
