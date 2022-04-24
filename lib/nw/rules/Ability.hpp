#pragma once

#include "system.hpp"

#include <array>
#include <initializer_list>

namespace nw {

// Since there is no 2da for abilities, this is a placeholder.

struct Ability {
    uint32_t name = 0xFFFFFFFF;
    Constant constant;
};

} // namespace nw
