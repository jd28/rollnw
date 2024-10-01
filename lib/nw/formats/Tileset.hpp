#pragma once

#include "../config.hpp"

#include <limits>
#include <stdint.h>
#include <string>
#include <vector>

namespace nw {

struct Tile {
    String model;
};

/// Abstraction of the SET tileset file.
struct Tileset {
    uint32_t strref = std::numeric_limits<uint32_t>::max();
    String name;
    std::vector<Tile> tiles;
    float tile_height = 5.0;
};

} // namespace nw
