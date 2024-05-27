#pragma once

#include <limits>
#include <stdint.h>
#include <string>
#include <vector>

namespace nw {

struct Tile {
    std::string model;
};

/// Abstraction of the SET tileset file.
struct Tileset {
    uint32_t strref = std::numeric_limits<uint32_t>::max();
    std::string name;
    std::vector<Tile> tiles;
    float tile_height = 5.0;
};

} // namespace nw
