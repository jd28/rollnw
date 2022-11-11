#pragma once

#include "constants.hpp"

namespace nw {
struct Effect;
struct ItemProperty;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

// -- Effect Creation ---------------------------------------------------------
// ----------------------------------------------------------------------------

/// Creates an ability modifier effect
nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier);

/// Creates a haste effect
nw::Effect* effect_haste();

// -- Effect Apply/Remove -----------------------------------------------------
// ----------------------------------------------------------------------------

bool effect_apply_is_creature(nw::ObjectBase* obj, const nw::Effect*);
bool effect_remove_is_creature(nw::ObjectBase* obj, const nw::Effect*);

bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*);
bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*);

// -- Item Property Creation --------------------------------------------------
// ----------------------------------------------------------------------------

/// Creates ability modifier item property
nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier);

/// Creates haste item property
nw::ItemProperty itemprop_haste();

// -- Item Property Generators ------------------------------------------------
// ----------------------------------------------------------------------------

nw::Effect* ip_gen_ability_modifier(const nw::ItemProperty& ip);

nw::Effect* ip_gen_haste(const nw::ItemProperty&);

} // namespace nwn1
