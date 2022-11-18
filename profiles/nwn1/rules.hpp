#pragma once

#include "functions.hpp"

#include <nw/components/ObjectBase.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const nw::ObjectBase* obj);
nw::RuleValue selector(const nw::Selector& selector, const nw::ObjectBase* obj);

// Generic
nw::ModifierFunction simple_feat_mod(nw::Feat feat, int value);

// Ability
nw::ModifierResult epic_great_ability(const nw::ObjectBase* obj, int32_t subtype);

// Armor Class
nw::ModifierResult dragon_disciple_ac(const nw::ObjectBase* obj);
nw::ModifierResult pale_master_ac(const nw::ObjectBase* obj);

// Attack Bonus
nw::ModifierResult enchant_arrow_ab(const nw::ObjectBase* obj, int32_t subtype);
nw::ModifierResult target_state_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target);
nw::ModifierResult weapon_master_ab(const nw::ObjectBase* obj, int32_t subtype);

// Damage Resist
nw::ModifierResult energy_resistance(const nw::ObjectBase* obj, int32_t subtype);

// Hitpoints
nw::ModifierResult toughness(const nw::ObjectBase* obj);
nw::ModifierResult epic_toughness(const nw::ObjectBase* obj);

} // namespace nwn1
