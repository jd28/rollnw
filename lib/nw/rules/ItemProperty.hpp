#pragma once

#include "rule_type.hpp"

#include <cstdint>
#include <limits>

namespace nw {

struct TwoDA;

DECLARE_RULE_TYPE(ItemPropertyType);

struct ItemProperty {
    uint16_t type = std::numeric_limits<uint16_t>::max();
    uint16_t subtype = std::numeric_limits<uint16_t>::max();
    uint8_t cost_table = std::numeric_limits<uint8_t>::max();
    uint16_t cost_value = std::numeric_limits<uint16_t>::max();
    uint8_t param_table = std::numeric_limits<uint8_t>::max();
    uint8_t param_value = std::numeric_limits<uint8_t>::max();
};

struct ItemPropertyDefinition {
    uint32_t name = std::numeric_limits<uint32_t>::max();
    const TwoDA* subtype = nullptr;
    float cost = 0.0f;
    const TwoDA* cost_table = nullptr;
    const TwoDA* param_table = nullptr;
    uint32_t game_string = std::numeric_limits<uint32_t>::max();
    uint32_t description = std::numeric_limits<uint32_t>::max();
    // Constants
};

} // namespace nw
