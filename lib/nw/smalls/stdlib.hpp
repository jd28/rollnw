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

} // namespace nw::smalls
