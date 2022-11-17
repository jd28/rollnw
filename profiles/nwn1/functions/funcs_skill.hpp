#pragma once

#include <nw/components/Creature.hpp>
#include <nw/rules/attributes.hpp>

namespace nwn1 {

/// Determines creatures skill rank
int get_skill_rank(const nw::Creature* obj, nw::Skill skill, bool base = false);

} // namespace nwn1
