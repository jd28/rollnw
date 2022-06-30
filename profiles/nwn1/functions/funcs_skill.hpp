#pragma once

#include <nw/rules/Skill.hpp>

#include <flecs/flecs.h>

namespace nwn1 {

/// Determines creatures skill rank
int get_skill_rank(flecs::entity ent, nw::Skill skill, bool base = false);

} // namespace nwn1
