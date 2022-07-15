#pragma once

#include <nw/components/Creature.hpp>
#include <nw/rules/Class.hpp>
#include <nw/util/EntityView.hpp>

namespace nwn1 {

/// Determines if monk class abilities are usable and monk class level
std::pair<bool, int> can_use_monk_abilities(flecs::entity ent);

} // namespace nw
