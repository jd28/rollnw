#pragma once

#include "functions.hpp"

#include <nw/rules/system.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const flecs::entity cre);
nw::RuleValue selector(const nw::Selector& selector, const flecs::entity ent);

// Armor Class
nw::ModifierResult dragon_disciple_ac(flecs::entity ent);
nw::ModifierResult pale_master_ac(flecs::entity ent);

// Hitpoints
nw::ModifierResult epic_toughness(flecs::entity ent);

} // namespace nwn1