#pragma once

#include "system.hpp"

namespace nw {

// Ignored 2da columns: Category, MaxCR

/// Skill definition
struct Skill {
    uint32_t name = 0xFFFFFFFF;
    uint32_t description;
    Resource icon;
    bool untrained = false;
    Index ability;
    bool armor_check_penalty = false;
    bool all_can_use = false;
    Index index;
    bool hostile = false;

    operator bool() const noexcept { return name != 0xFFFFFFFF; }
};

/// Singleton Component for Skils
struct SkillArray {
    std::vector<Skill> entries;
};

} // namespace nw
