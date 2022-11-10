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

// -- Item Property Creation --------------------------------------------------
// ----------------------------------------------------------------------------

nw::ItemProperty itemprop_ability_bonus(nw::Ability ability, int modifier);

/// Creates haste item property
nw::ItemProperty itemprop_haste();

// -- Item Property Generators ------------------------------------------------
// ----------------------------------------------------------------------------

nw::Effect* ip_gen_haste(const nw::ItemProperty&);

} // namespace nwn1
