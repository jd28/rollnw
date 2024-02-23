#pragma once

#include "constants.hpp"

namespace nw {
struct Creature;
struct ObjectBase;
}

namespace nwn1 {

/// Gets creature's caster level for specified class
int get_caster_level(nw::Creature* obj, nw::Class class_);

/// Gets spell DC
int get_spell_dc(nw::Creature* obj, nw::Class class_, nw::Spell spell);

} // namespace nwn1
