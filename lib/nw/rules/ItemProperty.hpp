#pragma once

#include "rule_type.hpp"

#include <cstdint>
#include <limits>

namespace nw {

DECLARE_RULE_TYPE(ItemPropertyType);

struct ItemProperty {
    uint16_t type = 0;
    uint16_t subtype = 0;
    uint8_t cost_table = 0;
    uint16_t cost_value = 0;
    uint8_t param_table = std::numeric_limits<uint8_t>::max();
    uint8_t param_value = std::numeric_limits<uint8_t>::max();
};

} // namespace nw
