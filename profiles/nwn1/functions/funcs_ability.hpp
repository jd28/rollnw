#pragma once

#include <nw/components/Creature.hpp>
#include <nw/rules/attributes.hpp>

namespace nwn1 {

/// Gets creatures ability score
int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false);

/// Gets creatures ability modifier
int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false);

} // namespace nwn1
