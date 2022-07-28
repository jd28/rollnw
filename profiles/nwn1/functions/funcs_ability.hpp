#pragma once

#include <nw/components/Creature.hpp>
#include <nw/rules/Ability.hpp>

namespace nwn1 {

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false);

} // namespace nwn1
