#pragma once

#include "../../rules/system.hpp"
#include "rule_helper_funcs.hpp"

namespace nwn1 {

bool match(const nw::Qualifier& qual, const flecs::entity cre);
nw::RuleValue selector(const nw::Selector& selector, const flecs::entity ent);

std::pair<nw::Index, int> has_feat_successor(flecs::entity ent, nw::Index feat);
size_t highest_feat_in_range(flecs::entity ent, size_t start, size_t end);

// Armor Class
nw::ModifierResult dragon_disciple_ac(flecs::entity ent);
nw::ModifierResult pale_master_ac(flecs::entity ent);

// Hitpoints
nw::ModifierResult epic_toughness(flecs::entity ent);

} // namespace nwn1
