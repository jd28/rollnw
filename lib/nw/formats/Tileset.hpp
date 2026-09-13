#pragma once

#include "../config.hpp"

#include <glm/vec3.hpp>

#include <limits>
#include <stdint.h>
#include <string>

namespace nw {

struct TileDoorSlot {
    glm::vec3 position{0.0f};
    float orientation = 0.0f;
    int32_t type = -1;
};

struct Tile {
    String model;
    // Raw SET routing hint. This is retained at the NWN adapter boundary for
    // corpus audit and possible long-range routing; local navigation does not
    // interpret it.
    String path_node;
    int32_t path_node_orientation = 0;
    Vector<TileDoorSlot> door_slots;
};

/// Abstraction of the SET tileset file.
struct Tileset {
    uint32_t strref = std::numeric_limits<uint32_t>::max();
    String name;
    Vector<Tile> tiles;
    float tile_height = 5.0;
};

} // namespace nw
