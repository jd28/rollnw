#pragma once

#include "../../rules/Index.hpp"

#include <flecs/flecs.h>

#include <vector>

namespace nwn1 {

/// Counts the number of known feats in the range [start, end]
int count_feats_in_range(flecs::entity ent, nw::Index start, nw::Index end);

/// Gets all feats for which requirements are met
/// @note This is not yet very useful until a level up parameter is added.
std::vector<size_t> get_all_available_feats(flecs::entity ent);

/// Gets the highest known successor feat
std::pair<nw::Index, int> has_feat_successor(flecs::entity ent, nw::Index feat);

/// Gets the highest known feat in range [start, end]
size_t highest_feat_in_range(flecs::entity ent, nw::Index start, nw::Index end);

/// Checks if an entity knows a given feat
/// @note this is a specific
bool knows_feat(flecs::entity ent, nw::Index feat);

} // namespace nwn1
