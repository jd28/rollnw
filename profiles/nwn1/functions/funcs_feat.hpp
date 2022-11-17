#pragma once

#include <nw/components/Creature.hpp>
#include <nw/rules/feats.hpp>

#include <array>
#include <vector>

namespace nwn1 {

/// Counts the number of known feats in the range [start, end]
int count_feats_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end);

/// Gets all feats for which requirements are met
/// @note This is not yet very useful until a level up parameter is added.
std::vector<nw::Feat> get_all_available_feats(const nw::Creature* obj);

/// Gets the highest known successor feat
std::pair<nw::Feat, int> has_feat_successor(const nw::Creature* obj, nw::Feat feat);

/// Gets the highest known feat in range [start, end]
nw::Feat highest_feat_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end);

/// Checks if an entity knows a given feat
bool knows_feat(const nw::Creature* obj, nw::Feat feat);

} // namespace nwn1
