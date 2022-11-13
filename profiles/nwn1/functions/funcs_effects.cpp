#include "funcs_effects.hpp"
#include "nw/rules/Effect.hpp"

namespace nwn1 {

int effect_extract_int0(const nw::EffectHandle& handle)
{
    return handle.effect ? handle.effect->get_int(0) : 0;
}

bool has_effect_applied(nw::ObjectBase* obj, nw::EffectType type, int subtype)
{
    if (!obj || type == nw::EffectType::invalid()) { return false; }
    auto it = find_first_effect_of(std::begin(obj->effects()), std::end(obj->effects()),
        type, subtype);

    return it != std::end(obj->effects());
}

} // namespace nwn1
