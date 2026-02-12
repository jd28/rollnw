#pragma once

#include "runtime.hpp"

namespace nw::smalls {

void register_core_prelude(Runtime& rt);
void register_core_test(Runtime& rt);
void register_core_string(Runtime& rt);
void register_core_array(Runtime& rt);
void register_core_map(Runtime& rt);
void register_core_math(Runtime& rt);
void register_core_effects(Runtime& rt);
void register_core_object(Runtime& rt);
void register_core_item(Runtime& rt);
void register_core_creature(Runtime& rt);
void register_core_area(Runtime& rt);
void register_core_door(Runtime& rt);
void register_core_encounter(Runtime& rt);
void register_core_module(Runtime& rt);
void register_core_placeable(Runtime& rt);
void register_core_player(Runtime& rt);
void register_core_sound(Runtime& rt);
void register_core_store(Runtime& rt);
void register_core_trigger(Runtime& rt);
void register_core_waypoint(Runtime& rt);

} // namespace nw::smalls
