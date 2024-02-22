#pragma once

#include "constants.hpp"

namespace nw {
struct Creature;
struct ObjectBase;
}

namespace nwn1 {

int get_caster_level(nw::Creature* obj, nw::Class class_);

} // namespace nwn1
