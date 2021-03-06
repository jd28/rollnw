#pragma once

#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/MasterFeat.hpp>
#include <nw/rules/system.hpp>
#include <nw/util/EntityView.hpp>

#include <flecs/flecs.h>

#include <array>
#include <vector>

namespace nwn1 {

/// Counts the number of known feats in the range [start, end]
int count_feats_in_range(flecs::entity ent, nw::Feat start, nw::Feat end);

/// Gets all feats for which requirements are met
/// @note This is not yet very useful until a level up parameter is added.
std::vector<nw::Feat> get_all_available_feats(flecs::entity ent);

/// Gets the highest known successor feat
std::pair<nw::Feat, int> has_feat_successor(flecs::entity ent, nw::Feat feat);

/// Gets the highest known feat in range [start, end]
nw::Feat highest_feat_in_range(flecs::entity ent, nw::Feat start, nw::Feat end);

/// Checks if an entity knows a given feat
bool knows_feat(flecs::entity ent, nw::Feat feat);

} // namespace nwn1
