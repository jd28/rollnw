#pragma once

#include "../resources/Resource.hpp"
#include "Constant.hpp"

#include <cstdint>

namespace nw {

/// Race definition
struct Race {
    uint32_t name = 0xFFFFFFFF;
    uint32_t name_conversation = 0xFFFFFFFF;
    uint32_t name_conversation_lower = 0xFFFFFFFF;
    uint32_t name_plural = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    int appearance = 0;
    std::array<int, 6> ability_modifiers;
    int favored_class = 0;
    Resource feats_table;
    uint32_t biography = 0xFFFFFFFF;
    bool player_race = false;
    Constant constant;
    int age = 1;
    int toolset_class = 0;
    float cr_modifier = 1.0f;
    int feats_extra_1st_level = 0;
    int skillpoints_extra_per_level = 0;
    int skillpoints_1st_level_multiplier = 0;
    int ability_point_buy_number = 0;
    int feats_normal_level = 0;
    int feats_normal_amount = 0;
    int skillpoints_ability = 0;

    operator bool() const noexcept { return name != 0xFFFFFFFF; }
};

/// Race singleton component
struct RaceArray {
    std::vector<Race> races;
};

// Not Implemented Yet
// - NameGenTableA
// - NameGenTableB

// Unimplemented
// - Endurance

} // namespace nw
