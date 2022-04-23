#pragma once

#include "system.hpp"

namespace nw {

// Ignored 2da columns: Category, MaxCR

struct Skill {
    uint32_t name = 0xFFFFFFFF;
    uint32_t description;
    Resource icon;
    bool untrained = false;
    Constant ability;
    bool armor_check_penalty = false;
    bool all_can_use = false;
    Constant constant;
    bool hostile = false;

    operator bool() const noexcept { return name != 0xFFFFFFFF; }
};

} // namespace nw
