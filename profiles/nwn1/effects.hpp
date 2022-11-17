#pragma once

#include "constants.hpp"
#include "nw/components/Equips.hpp"
#include "nw/rules/combat.hpp"

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

/// Creates an attack modifier effect
nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier);

/// Creates a haste effect
nw::Effect* effect_haste();

/// Creates an skill modifier effect
nw::Effect* effect_skill_modifier(nw::Skill skill, int modifier);

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

/// Creates attack modifier item property
nw::ItemProperty itemprop_attack_modifier(int value);

/// Creates enhancement modifier item property
nw::ItemProperty itemprop_enhancement_modifier(int value);

/// Creates haste item property
nw::ItemProperty itemprop_haste();

/// Creates skill modifier item property
nw::ItemProperty itemprop_skill_modifier(nw::Skill skill, int modifier);

// -- Item Property Generators ------------------------------------------------
// ----------------------------------------------------------------------------

nw::Effect* ip_gen_ability_modifier(const nw::ItemProperty& ip, nw::EquipIndex);
nw::Effect* ip_gen_attack_modifier(const nw::ItemProperty& ip, nw::EquipIndex);
nw::Effect* ip_gen_enhancement_modifier(const nw::ItemProperty& ip, nw::EquipIndex);
nw::Effect* ip_gen_haste(const nw::ItemProperty&, nw::EquipIndex);
nw::Effect* ip_gen_skill_modifier(const nw::ItemProperty& ip, nw::EquipIndex);

} // namespace nwn1
