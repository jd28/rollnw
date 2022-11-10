#pragma once

#include "constants.hpp"

namespace nw {
struct Effect;
struct ItemProperty;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

/// Creates a haste effect
nw::Effect* effect_haste();
bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*);
bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*);

nw::Effect* ip_gen_haste(const nw::ItemProperty& ip);

} // namespace nwn1
