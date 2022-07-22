#pragma once

#include "functions.hpp"

#include <nw/rules/system.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const flecs::entity cre);
nw::RuleValue selector(const nw::Selector& selector, const flecs::entity ent);

// Ability
nw::ModifierResult epic_great_strength(flecs::entity ent);
nw::ModifierResult epic_great_dexterity(flecs::entity ent);
nw::ModifierResult epic_great_constitution(flecs::entity ent);
nw::ModifierResult epic_great_intelligence(flecs::entity ent);
nw::ModifierResult epic_great_wisdom(flecs::entity ent);
nw::ModifierResult epic_great_charisma(flecs::entity ent);

// Armor Class
nw::ModifierResult dragon_disciple_ac(flecs::entity ent);
nw::ModifierResult pale_master_ac(flecs::entity ent);

// Hitpoints
nw::ModifierResult toughness(flecs::entity ent);
nw::ModifierResult epic_toughness(flecs::entity ent);

} // namespace nwn1
