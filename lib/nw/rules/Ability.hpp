#pragma once

#include "Index.hpp"

#include <vector>

namespace nw {

// Since there is no 2da for abilities, this is a placeholder.

struct Ability {
    uint32_t name = 0xFFFFFFFF;
    Index index;
};

/// Ability singleton component
struct AbilityArray {
    std::vector<Ability> entries;
};

} // namespace nw
