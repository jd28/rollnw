#pragma once

#include "../config.hpp"

#include <glm/vec3.hpp>

#include <array>
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
    String image_map_2d;
    // Raw SET routing hint. This is retained at the NWN adapter boundary for
    // corpus audit and possible long-range routing; local navigation does not
    // interpret it.
    String path_node;
    int32_t path_node_orientation = 0;
    Vector<TileDoorSlot> door_slots;
};

struct TilesetNamedType {
    String name;
};

// Cold authoring data parallel to Tileset::tiles. Terrain and crosser values
// index the SET catalogs; -1 means no crosser. Heights are relative to the
// AreaTile height.
struct TilesetTileTopology {
    std::array<int32_t, 4> terrain{-1, -1, -1, -1};
    std::array<int32_t, 4> height{};
    std::array<int32_t, 4> crosser{-1, -1, -1, -1};
    bool valid = false;
};

struct TilesetGroup {
    uint32_t rows = 0;
    uint32_t columns = 0;
    uint32_t tile_offset = 0;
    uint32_t tile_count = 0;
};

/// Abstraction of the SET tileset file.
struct Tileset {
    uint32_t strref = std::numeric_limits<uint32_t>::max();
    String name;
    Vector<Tile> tiles;
    Vector<TilesetNamedType> terrains;
    Vector<TilesetNamedType> crossers;
    Vector<TilesetTileTopology> tile_topologies;
    Vector<TilesetGroup> groups;
    Vector<int32_t> group_tile_ids;
    Vector<uint8_t> grouped_tiles;
    float tile_height = 5.0;
    int32_t default_terrain = -1;
    bool has_height_transition = false;
};

} // namespace nw
