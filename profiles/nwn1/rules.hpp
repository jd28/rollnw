#pragma once

#include "functions.hpp"

#include <nw/components/ObjectBase.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const nw::ObjectBase* obj);
nw::RuleValue selector(const nw::Selector& selector, const nw::ObjectBase* obj);

// Ability
nw::ModifierResult epic_great_strength(const nw::ObjectBase* obj);
nw::ModifierResult epic_great_dexterity(const nw::ObjectBase* obj);
nw::ModifierResult epic_great_constitution(const nw::ObjectBase* obj);
nw::ModifierResult epic_great_intelligence(const nw::ObjectBase* obj);
nw::ModifierResult epic_great_wisdom(const nw::ObjectBase* obj);
nw::ModifierResult epic_great_charisma(const nw::ObjectBase* obj);

// Armor Class
nw::ModifierResult dragon_disciple_ac(const nw::ObjectBase* obj);
nw::ModifierResult pale_master_ac(const nw::ObjectBase* obj);

// Hitpoints
nw::ModifierResult toughness(const nw::ObjectBase* obj);
nw::ModifierResult epic_toughness(const nw::ObjectBase* obj);

} // namespace nwn1
