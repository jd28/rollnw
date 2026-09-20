#include "area_tile_brush.hpp"

#include "area_door_hooks.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace nw::toolset {
namespace {

constexpr int32_t unset_value = std::numeric_limits<int32_t>::min();
constexpr int64_t unset_height = std::numeric_limits<int64_t>::min();

struct AreaTopology {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<int32_t> corner_terrains;
    std::vector<int64_t> corner_heights;
    std::vector<int32_t> horizontal_crossers;
    std::vector<int32_t> vertical_crossers;
};

struct TileChoice {
    int32_t id = -1;
    int32_t orientation = 0;
    int32_t height = 0;
};

enum class TileFitMode : uint8_t {
    preserve_current,
    random_other,
    canonical,
};

constexpr uint8_t fit_force = 1u << 0u;
constexpr uint8_t fit_replace_group = 1u << 1u;
constexpr uint8_t fit_canonical = 1u << 2u;

ObjectEditApplyResult brush_result(
    ObjectEditStatus status, std::string diagnostic = {})
{
    return {
        .status = status,
        .diagnostic = std::move(diagnostic),
    };
}

size_t corner_index(const AreaTopology& topology, int32_t x, int32_t y)
{
    return static_cast<size_t>(y)
        * static_cast<size_t>(topology.width + 1)
        + static_cast<size_t>(x);
}

size_t horizontal_edge_index(
    const AreaTopology& topology, int32_t x, int32_t y)
{
    return static_cast<size_t>(y) * static_cast<size_t>(topology.width)
        + static_cast<size_t>(x);
}

size_t vertical_edge_index(
    const AreaTopology& topology, int32_t x, int32_t y)
{
    return static_cast<size_t>(y)
        * static_cast<size_t>(topology.width + 1)
        + static_cast<size_t>(x);
}

constexpr std::array<uint8_t, 4> world_corner_to_local(
    int32_t orientation) noexcept
{
    switch (orientation) {
    case 0:
        return {0, 1, 2, 3};
    case 1:
        return {1, 3, 0, 2};
    case 2:
        return {3, 2, 1, 0};
    case 3:
        return {2, 0, 3, 1};
    default:
        return {};
    }
}

constexpr std::array<uint8_t, 4> world_edge_to_local(
    int32_t orientation) noexcept
{
    switch (orientation) {
    case 0:
        return {0, 1, 2, 3};
    case 1:
        return {1, 2, 3, 0};
    case 2:
        return {2, 3, 0, 1};
    case 3:
        return {3, 0, 1, 2};
    default:
        return {};
    }
}

bool merge_value(int32_t& destination, int32_t value) noexcept
{
    if (destination == unset_value) {
        destination = value;
        return true;
    }
    return destination == value;
}

bool merge_height(int64_t& destination, int64_t value) noexcept
{
    if (destination == unset_height) {
        destination = value;
        return true;
    }
    return destination == value;
}

bool merge_tile_topology(AreaTopology& output,
    int32_t x,
    int32_t y,
    const TilesetTileTopology& tile,
    int32_t orientation,
    int32_t base_height)
{
    const auto corners = world_corner_to_local(orientation);
    const std::array<std::array<int32_t, 2>, 4> corner_positions{{{x, y + 1}, {x + 1, y + 1}, {x, y}, {x + 1, y}}};
    for (size_t world = 0; world < corners.size(); ++world) {
        const size_t destination = corner_index(output,
            corner_positions[world][0], corner_positions[world][1]);
        const size_t local = corners[world];
        if (!merge_value(
                output.corner_terrains[destination], tile.terrain[local])
            || !merge_height(output.corner_heights[destination],
                static_cast<int64_t>(base_height) + tile.height[local])) {
            return false;
        }
    }

    const auto edges = world_edge_to_local(orientation);
    return merge_value(output.horizontal_crossers[horizontal_edge_index(output, x, y + 1)],
               tile.crosser[edges[0]])
        && merge_value(output.vertical_crossers[vertical_edge_index(output, x + 1, y)],
            tile.crosser[edges[1]])
        && merge_value(output.horizontal_crossers[horizontal_edge_index(output, x, y)],
            tile.crosser[edges[2]])
        && merge_value(output.vertical_crossers[vertical_edge_index(output, x, y)],
            tile.crosser[edges[3]]);
}

bool decode_area_topology(
    const Area& area, AreaTopology& output, std::string& diagnostic)
{
    if (!area.tileset || area.width <= 0 || area.height <= 0
        || area.width == std::numeric_limits<int32_t>::max()
        || area.height == std::numeric_limits<int32_t>::max()
        || static_cast<uint64_t>(area.width)
                * static_cast<uint64_t>(area.height)
            != area.tiles.size()
        || area.tileset->tile_topologies.size()
            != area.tileset->tiles.size()) {
        diagnostic = "Area or SET topology is unavailable";
        return false;
    }

    output = {
        .width = area.width,
        .height = area.height,
        .corner_terrains = std::vector<int32_t>(
            static_cast<size_t>(area.width + 1)
                * static_cast<size_t>(area.height + 1),
            unset_value),
        .corner_heights = std::vector<int64_t>(
            static_cast<size_t>(area.width + 1)
                * static_cast<size_t>(area.height + 1),
            unset_height),
        .horizontal_crossers = std::vector<int32_t>(
            static_cast<size_t>(area.width)
                * static_cast<size_t>(area.height + 1),
            unset_value),
        .vertical_crossers = std::vector<int32_t>(
            static_cast<size_t>(area.width + 1)
                * static_cast<size_t>(area.height),
            unset_value),
    };

    for (int32_t y = 0; y < area.height; ++y) {
        for (int32_t x = 0; x < area.width; ++x) {
            const size_t index = static_cast<size_t>(y)
                    * static_cast<size_t>(area.width)
                + static_cast<size_t>(x);
            const auto& row = area.tiles[index];
            if (area_tile_is_void(row)) {
                continue;
            }
            if (row.id < 0
                || static_cast<size_t>(row.id)
                    >= area.tileset->tile_topologies.size()
                || row.orientation < 0 || row.orientation >= 4
                || !area.tileset->tile_topologies[static_cast<size_t>(row.id)]
                    .valid) {
                diagnostic = "Tile " + std::to_string(index)
                    + " has no valid SET topology";
                return false;
            }
            if (!merge_tile_topology(output, x, y,
                    area.tileset->tile_topologies[static_cast<size_t>(row.id)],
                    row.orientation, row.height)) {
                diagnostic = "Area has an inconsistent SET seam at tile "
                    + std::to_string(index);
                return false;
            }
        }
    }
    return true;
}

std::array<int32_t, 4> cell_terrains(
    const AreaTopology& topology, int32_t x, int32_t y)
{
    return {
        topology.corner_terrains[corner_index(topology, x, y + 1)],
        topology.corner_terrains[corner_index(topology, x + 1, y + 1)],
        topology.corner_terrains[corner_index(topology, x, y)],
        topology.corner_terrains[corner_index(topology, x + 1, y)],
    };
}

std::array<int64_t, 4> cell_heights(
    const AreaTopology& topology, int32_t x, int32_t y)
{
    return {
        topology.corner_heights[corner_index(topology, x, y + 1)],
        topology.corner_heights[corner_index(topology, x + 1, y + 1)],
        topology.corner_heights[corner_index(topology, x, y)],
        topology.corner_heights[corner_index(topology, x + 1, y)],
    };
}

std::array<int32_t, 4> cell_crossers(
    const AreaTopology& topology, int32_t x, int32_t y)
{
    return {
        topology.horizontal_crossers[horizontal_edge_index(topology, x, y + 1)],
        topology.vertical_crossers[vertical_edge_index(topology, x + 1, y)],
        topology.horizontal_crossers[horizontal_edge_index(topology, x, y)],
        topology.vertical_crossers[vertical_edge_index(topology, x, y)],
    };
}

bool choice_matches(const Tileset& tileset,
    int32_t tile_id,
    int32_t orientation,
    const std::array<int32_t, 4>& terrains,
    const std::array<int64_t, 4>& heights,
    const std::array<int32_t, 4>& crossers,
    TileChoice& output) noexcept
{
    if (tile_id < 0
        || static_cast<size_t>(tile_id) >= tileset.tile_topologies.size()) {
        return false;
    }
    const auto& tile = tileset.tile_topologies[static_cast<size_t>(tile_id)];
    if (!tile.valid || orientation < 0 || orientation >= 4) {
        return false;
    }
    const auto corners = world_corner_to_local(orientation);
    const auto edges = world_edge_to_local(orientation);
    const int64_t base_height
        = heights[0] - tile.height[corners[0]];
    if (base_height < std::numeric_limits<int32_t>::min()
        || base_height > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    for (size_t index = 0; index < 4; ++index) {
        if (terrains[index] != tile.terrain[corners[index]]
            || heights[index]
                != base_height + tile.height[corners[index]]
            || crossers[index] != tile.crosser[edges[index]]) {
            return false;
        }
    }
    output = {
        .id = tile_id,
        .orientation = orientation,
        .height = static_cast<int32_t>(base_height),
    };
    return true;
}

uint64_t next_random(uint64_t& state) noexcept
{
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t value = state;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31u);
}

uint64_t uniform_random(uint64_t& state, uint64_t bound) noexcept
{
    const uint64_t threshold = (0u - bound) % bound;
    for (;;) {
        const uint64_t value = next_random(state);
        if (value >= threshold) {
            return value % bound;
        }
    }
}

bool fit_cell(const Area& area,
    const AreaTopology& topology,
    uint32_t tile_index,
    TileFitMode mode,
    uint64_t& random_state,
    TileChoice& output)
{
    const int32_t x = static_cast<int32_t>(
        tile_index % static_cast<uint32_t>(area.width));
    const int32_t y = static_cast<int32_t>(
        tile_index / static_cast<uint32_t>(area.width));
    const auto terrains = cell_terrains(topology, x, y);
    const auto heights = cell_heights(topology, x, y);
    const auto crossers = cell_crossers(topology, x, y);
    const auto& current = area.tiles[tile_index];
    if (mode == TileFitMode::preserve_current
        && choice_matches(*area.tileset, current.id, current.orientation,
            terrains, heights, crossers, output)
        && output.height == current.height) {
        return true;
    }

    const auto candidate_matches
        = [&](size_t tile_id, int32_t orientation, TileChoice& candidate) {
              if (tile_id < area.tileset->grouped_tiles.size()
                  && area.tileset->grouped_tiles[tile_id] != 0) {
                  return false;
              }
              if (!choice_matches(*area.tileset,
                      static_cast<int32_t>(tile_id), orientation,
                      terrains, heights, crossers, candidate)) {
                  return false;
              }
              return mode != TileFitMode::random_other
                  || candidate.id != current.id
                  || candidate.orientation != current.orientation
                  || candidate.height != current.height;
          };

    TileChoice chosen;
    uint64_t match_count = 0;
    for (size_t tile_id = 0;
        tile_id < area.tileset->tile_topologies.size(); ++tile_id) {
        for (int32_t orientation = 0; orientation < 4; ++orientation) {
            TileChoice candidate;
            if (candidate_matches(tile_id, orientation, candidate)) {
                if (mode == TileFitMode::canonical) {
                    output = candidate;
                    return true;
                }
                ++match_count;
                if (match_count == 1) {
                    chosen = candidate;
                }
            }
        }
    }
    if (match_count == 0 && mode == TileFitMode::random_other
        && choice_matches(*area.tileset, current.id, current.orientation,
            terrains, heights, crossers, chosen)
        && static_cast<size_t>(current.id)
            < area.tileset->grouped_tiles.size()
        && area.tileset->grouped_tiles[static_cast<size_t>(current.id)] == 0) {
        match_count = 1;
    }
    if (match_count == 0) {
        return false;
    }
    if (match_count == 1) {
        output = chosen;
        return true;
    }

    const uint64_t selected = uniform_random(random_state, match_count);
    uint64_t match_index = 0;
    for (size_t tile_id = 0;
        tile_id < area.tileset->tile_topologies.size(); ++tile_id) {
        for (int32_t orientation = 0; orientation < 4; ++orientation) {
            TileChoice candidate;
            if (!candidate_matches(tile_id, orientation, candidate)) {
                continue;
            }
            if (match_index++ == selected) {
                output = candidate;
                return true;
            }
        }
    }
    return false;
}

bool next_cell_variation(const Area& area,
    const AreaTopology& topology,
    uint32_t tile_index,
    TileChoice& output)
{
    const auto& current = area.tiles[tile_index];
    if (current.id < 0
        || static_cast<size_t>(current.id)
            >= area.tileset->grouped_tiles.size()
        || area.tileset->grouped_tiles[static_cast<size_t>(current.id)] != 0) {
        return false;
    }
    const int32_t x = static_cast<int32_t>(
        tile_index % static_cast<uint32_t>(area.width));
    const int32_t y = static_cast<int32_t>(
        tile_index / static_cast<uint32_t>(area.width));
    const auto terrains = cell_terrains(topology, x, y);
    const auto heights = cell_heights(topology, x, y);
    const auto crossers = cell_crossers(topology, x, y);
    TileChoice first;
    bool found_current = false;
    for (size_t tile_id = 0;
        tile_id < area.tileset->tile_topologies.size(); ++tile_id) {
        if (tile_id < area.tileset->grouped_tiles.size()
            && area.tileset->grouped_tiles[tile_id] != 0) {
            continue;
        }
        for (int32_t orientation = 0; orientation < 4; ++orientation) {
            TileChoice candidate;
            if (!choice_matches(*area.tileset,
                    static_cast<int32_t>(tile_id), orientation,
                    terrains, heights, crossers, candidate)) {
                continue;
            }
            if (first.id < 0) {
                first = candidate;
            }
            if (found_current) {
                output = candidate;
                return true;
            }
            found_current = candidate.id == current.id
                && candidate.orientation == current.orientation
                && candidate.height == current.height;
        }
    }
    if (found_current
        && (first.id != current.id
            || first.orientation != current.orientation
            || first.height != current.height)) {
        output = first;
        return true;
    }
    return false;
}

void mark_cell(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::vector<uint8_t>& affected) noexcept
{
    if (x >= 0 && x < topology.width && y >= 0 && y < topology.height) {
        affected[static_cast<size_t>(y) * static_cast<size_t>(topology.width)
            + static_cast<size_t>(x)]
            = 1;
    }
}

void mark_corner_incident(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::vector<uint8_t>& affected) noexcept
{
    mark_cell(topology, x - 1, y - 1, affected);
    mark_cell(topology, x, y - 1, affected);
    mark_cell(topology, x - 1, y, affected);
    mark_cell(topology, x, y, affected);
}

void mark_horizontal_edge_incident(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::vector<uint8_t>& affected) noexcept
{
    mark_cell(topology, x, y - 1, affected);
    mark_cell(topology, x, y, affected);
}

void mark_vertical_edge_incident(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::vector<uint8_t>& affected) noexcept
{
    mark_cell(topology, x - 1, y, affected);
    mark_cell(topology, x, y, affected);
}

bool set_terrain_cells(const Area& area,
    std::span<const uint32_t> cells,
    int32_t terrain,
    AreaTopology& topology,
    std::vector<uint8_t>& affected,
    std::vector<uint8_t>& fit_flags)
{
    if (terrain < 0
        || static_cast<size_t>(terrain) >= area.tileset->terrains.size()) {
        return false;
    }
    for (const uint32_t tile_index : cells) {
        const auto& current = area.tiles[tile_index];
        const bool filling_void = area_tile_is_void(current);
        const int32_t x = static_cast<int32_t>(
            tile_index % static_cast<uint32_t>(area.width));
        const int32_t y = static_cast<int32_t>(
            tile_index / static_cast<uint32_t>(area.width));
        const std::array<std::array<int32_t, 2>, 4> corners{{{x, y + 1}, {x + 1, y + 1}, {x, y}, {x + 1, y}}};
        for (const auto& position : corners) {
            const size_t index
                = corner_index(topology, position[0], position[1]);
            topology.corner_terrains[index] = terrain;
            if (filling_void
                && topology.corner_heights[index] == unset_height) {
                topology.corner_heights[index] = current.height;
            }
            mark_corner_incident(
                topology, position[0], position[1], affected);
        }
        topology.horizontal_crossers[horizontal_edge_index(topology, x, y + 1)]
            = -1;
        topology.vertical_crossers[vertical_edge_index(topology, x + 1, y)]
            = -1;
        topology.horizontal_crossers[horizontal_edge_index(topology, x, y)]
            = -1;
        topology.vertical_crossers[vertical_edge_index(topology, x, y)]
            = -1;
        mark_horizontal_edge_incident(topology, x, y + 1, affected);
        mark_vertical_edge_incident(topology, x + 1, y, affected);
        mark_horizontal_edge_incident(topology, x, y, affected);
        mark_vertical_edge_incident(topology, x, y, affected);
        fit_flags[tile_index] |= fit_force;
    }
    return true;
}

void build_placed_group_cell_mask(
    const Area& area, std::vector<uint8_t>& output);

bool corner_touches_unselected_group(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::span<const uint8_t> selected,
    std::span<const uint8_t> group_cells) noexcept
{
    for (int32_t cell_y = y - 1; cell_y <= y; ++cell_y) {
        for (int32_t cell_x = x - 1; cell_x <= x; ++cell_x) {
            if (cell_x < 0 || cell_x >= topology.width || cell_y < 0
                || cell_y >= topology.height) {
                continue;
            }
            const size_t tile_index
                = static_cast<size_t>(cell_y)
                    * static_cast<size_t>(topology.width)
                + static_cast<size_t>(cell_x);
            if (selected[tile_index] == 0
                && group_cells[tile_index] != 0) {
                return true;
            }
        }
    }
    return false;
}

bool horizontal_edge_touches_unselected_group(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::span<const uint8_t> selected,
    std::span<const uint8_t> group_cells) noexcept
{
    for (const int32_t cell_y : {y - 1, y}) {
        if (cell_y < 0 || cell_y >= topology.height) {
            continue;
        }
        const size_t tile_index
            = static_cast<size_t>(cell_y)
                * static_cast<size_t>(topology.width)
            + static_cast<size_t>(x);
        if (selected[tile_index] == 0 && group_cells[tile_index] != 0) {
            return true;
        }
    }
    return false;
}

bool vertical_edge_touches_unselected_group(const AreaTopology& topology,
    int32_t x,
    int32_t y,
    std::span<const uint8_t> selected,
    std::span<const uint8_t> group_cells) noexcept
{
    for (const int32_t cell_x : {x - 1, x}) {
        if (cell_x < 0 || cell_x >= topology.width) {
            continue;
        }
        const size_t tile_index
            = static_cast<size_t>(y)
                * static_cast<size_t>(topology.width)
            + static_cast<size_t>(cell_x);
        if (selected[tile_index] == 0 && group_cells[tile_index] != 0) {
            return true;
        }
    }
    return false;
}

bool set_eraser_cells(const Area& area,
    std::span<const uint32_t> cells,
    int32_t terrain,
    AreaTopology& topology,
    std::vector<uint8_t>& affected,
    std::vector<uint8_t>& fit_flags)
{
    if (terrain < 0
        || static_cast<size_t>(terrain) >= area.tileset->terrains.size()) {
        return false;
    }

    std::vector<uint8_t> selected(area.tiles.size(), 0);
    for (const uint32_t tile_index : cells) {
        selected[tile_index] = 1;
    }
    std::vector<uint8_t> group_cells;
    build_placed_group_cell_mask(area, group_cells);

    for (const uint32_t tile_index : cells) {
        const int32_t x = static_cast<int32_t>(
            tile_index % static_cast<uint32_t>(area.width));
        const int32_t y = static_cast<int32_t>(
            tile_index / static_cast<uint32_t>(area.width));
        const std::array<std::array<int32_t, 2>, 4> corners{{{x, y + 1}, {x + 1, y + 1}, {x, y}, {x + 1, y}}};
        for (const auto& position : corners) {
            if (corner_touches_unselected_group(topology,
                    position[0], position[1], selected, group_cells)) {
                continue;
            }
            topology.corner_terrains[corner_index(
                topology, position[0], position[1])]
                = terrain;
            mark_corner_incident(
                topology, position[0], position[1], affected);
        }

        if (!horizontal_edge_touches_unselected_group(
                topology, x, y + 1, selected, group_cells)) {
            topology.horizontal_crossers[horizontal_edge_index(
                topology, x, y + 1)]
                = -1;
            mark_horizontal_edge_incident(topology, x, y + 1, affected);
        }
        if (!vertical_edge_touches_unselected_group(
                topology, x + 1, y, selected, group_cells)) {
            topology.vertical_crossers[vertical_edge_index(
                topology, x + 1, y)]
                = -1;
            mark_vertical_edge_incident(topology, x + 1, y, affected);
        }
        if (!horizontal_edge_touches_unselected_group(
                topology, x, y, selected, group_cells)) {
            topology.horizontal_crossers[horizontal_edge_index(
                topology, x, y)]
                = -1;
            mark_horizontal_edge_incident(topology, x, y, affected);
        }
        if (!vertical_edge_touches_unselected_group(
                topology, x, y, selected, group_cells)) {
            topology.vertical_crossers[vertical_edge_index(
                topology, x, y)]
                = -1;
            mark_vertical_edge_incident(topology, x, y, affected);
        }
        affected[tile_index] = 1;
        fit_flags[tile_index]
            |= fit_force | fit_replace_group | fit_canonical;
    }
    return true;
}

bool change_corner_heights(const Area& area,
    std::span<const uint32_t> corners,
    int32_t delta,
    AreaTopology& topology,
    std::vector<uint8_t>& affected)
{
    if (!area.tileset->has_height_transition || (delta != -1 && delta != 1)) {
        return false;
    }
    for (const uint32_t index : corners) {
        if (index >= topology.corner_heights.size()) {
            return false;
        }
        const int64_t value = topology.corner_heights[index];
        if (value == unset_height
            || (delta > 0 && value == std::numeric_limits<int64_t>::max())
            || (delta < 0
                && value == std::numeric_limits<int64_t>::min())) {
            return false;
        }
        topology.corner_heights[index] += delta;
        const int32_t x = static_cast<int32_t>(
            index % static_cast<uint32_t>(topology.width + 1));
        const int32_t y = static_cast<int32_t>(
            index / static_cast<uint32_t>(topology.width + 1));
        mark_corner_incident(topology, x, y, affected);
    }
    return true;
}

void set_crosser_edge(AreaTopology& topology,
    int32_t from_x,
    int32_t from_y,
    int32_t to_x,
    int32_t to_y,
    int32_t crosser,
    std::vector<uint8_t>& affected,
    std::vector<uint8_t>& fit_flags)
{
    if (to_x == from_x + 1) {
        topology.vertical_crossers[vertical_edge_index(topology, from_x + 1, from_y)]
            = crosser;
        mark_vertical_edge_incident(
            topology, from_x + 1, from_y, affected);
    } else if (to_x == from_x - 1) {
        topology.vertical_crossers[vertical_edge_index(topology, from_x, from_y)]
            = crosser;
        mark_vertical_edge_incident(topology, from_x, from_y, affected);
    } else if (to_y == from_y + 1) {
        topology.horizontal_crossers[horizontal_edge_index(topology, from_x, from_y + 1)]
            = crosser;
        mark_horizontal_edge_incident(
            topology, from_x, from_y + 1, affected);
    } else if (to_y == from_y - 1) {
        topology.horizontal_crossers[horizontal_edge_index(topology, from_x, from_y)]
            = crosser;
        mark_horizontal_edge_incident(topology, from_x, from_y, affected);
    }
    mark_cell(topology, from_x, from_y, fit_flags);
    mark_cell(topology, to_x, to_y, fit_flags);
}

bool set_crosser_path(const Area& area,
    std::span<const uint32_t> cells,
    int32_t crosser,
    AreaTopology& topology,
    std::vector<uint8_t>& affected,
    std::vector<uint8_t>& fit_flags)
{
    if (crosser < 0
        || static_cast<size_t>(crosser) >= area.tileset->crossers.size()
        || cells.size() < 2) {
        return false;
    }
    int32_t x = static_cast<int32_t>(
        cells.front() % static_cast<uint32_t>(area.width));
    int32_t y = static_cast<int32_t>(
        cells.front() / static_cast<uint32_t>(area.width));
    for (size_t index = 1; index < cells.size(); ++index) {
        const int32_t target_x = static_cast<int32_t>(
            cells[index] % static_cast<uint32_t>(area.width));
        const int32_t target_y = static_cast<int32_t>(
            cells[index] / static_cast<uint32_t>(area.width));
        while (x != target_x) {
            const int32_t next_x = x + (target_x > x ? 1 : -1);
            set_crosser_edge(topology, x, y, next_x, y, crosser,
                affected, fit_flags);
            x = next_x;
        }
        while (y != target_y) {
            const int32_t next_y = y + (target_y > y ? 1 : -1);
            set_crosser_edge(topology, x, y, x, next_y, crosser,
                affected, fit_flags);
            y = next_y;
        }
    }
    return true;
}

struct RotatedGroupCell {
    uint32_t x = 0;
    uint32_t y = 0;
};

RotatedGroupCell rotate_group_cell(const TilesetGroup& group,
    uint32_t row,
    uint32_t column,
    int32_t orientation) noexcept
{
    switch (orientation) {
    case 1:
        return {
            .x = group.rows - 1 - row,
            .y = column,
        };
    case 2:
        return {
            .x = group.columns - 1 - column,
            .y = group.rows - 1 - row,
        };
    case 3:
        return {
            .x = row,
            .y = group.columns - 1 - column,
        };
    default:
        return {
            .x = column,
            .y = row,
        };
    }
}

struct PlacedGroupFootprint {
    uint32_t group_index = UINT32_MAX;
    int32_t anchor_x = 0;
    int32_t anchor_y = 0;
    int32_t orientation = 0;
    uint32_t columns = 0;
    uint32_t rows = 0;
};

bool same_footprint(const PlacedGroupFootprint& lhs,
    const PlacedGroupFootprint& rhs) noexcept
{
    return lhs.anchor_x == rhs.anchor_x && lhs.anchor_y == rhs.anchor_y
        && lhs.columns == rhs.columns && lhs.rows == rhs.rows;
}

bool placed_group_matches(const Area& area,
    const TilesetGroup& group,
    uint32_t source_row,
    uint32_t source_column,
    uint32_t clicked_index,
    PlacedGroupFootprint& output) noexcept
{
    const auto& clicked = area.tiles[clicked_index];
    const auto clicked_offset = rotate_group_cell(
        group, source_row, source_column, clicked.orientation);
    const int32_t clicked_x = static_cast<int32_t>(
        clicked_index % static_cast<uint32_t>(area.width));
    const int32_t clicked_y = static_cast<int32_t>(
        clicked_index / static_cast<uint32_t>(area.width));
    const int32_t anchor_x
        = clicked_x - static_cast<int32_t>(clicked_offset.x);
    const int32_t anchor_y
        = clicked_y - static_cast<int32_t>(clicked_offset.y);
    const uint32_t output_columns
        = clicked.orientation % 2 == 0 ? group.columns : group.rows;
    const uint32_t output_rows
        = clicked.orientation % 2 == 0 ? group.rows : group.columns;
    if (anchor_x < 0 || anchor_y < 0
        || output_columns > static_cast<uint32_t>(area.width - anchor_x)
        || output_rows > static_cast<uint32_t>(area.height - anchor_y)) {
        return false;
    }

    for (uint32_t row = 0; row < group.rows; ++row) {
        for (uint32_t column = 0; column < group.columns; ++column) {
            const uint32_t source_index = row * group.columns + column;
            const int32_t tile_id = area.tileset->group_tile_ids[static_cast<size_t>(group.tile_offset + source_index)];
            if (tile_id < 0) {
                continue;
            }
            const auto offset = rotate_group_cell(
                group, row, column, clicked.orientation);
            const uint32_t area_index = static_cast<uint32_t>(
                (anchor_y + static_cast<int32_t>(offset.y)) * area.width
                + anchor_x + static_cast<int32_t>(offset.x));
            const auto& tile = area.tiles[area_index];
            if (tile.id != tile_id || tile.orientation != clicked.orientation
                || tile.height != clicked.height) {
                return false;
            }
        }
    }
    output = {
        .anchor_x = anchor_x,
        .anchor_y = anchor_y,
        .orientation = clicked.orientation,
        .columns = output_columns,
        .rows = output_rows,
    };
    return true;
}

bool resolve_placed_group(const Area& area,
    uint32_t clicked_index,
    PlacedGroupFootprint& output,
    std::string& diagnostic)
{
    const auto& clicked = area.tiles[clicked_index];
    bool found = false;
    for (uint32_t group_index = 0;
        group_index < area.tileset->groups.size(); ++group_index) {
        const auto& group = area.tileset->groups[group_index];
        if (static_cast<uint64_t>(group.rows) * group.columns
                != group.tile_count
            || static_cast<uint64_t>(group.tile_offset) + group.tile_count
                > area.tileset->group_tile_ids.size()) {
            continue;
        }
        for (uint32_t row = 0; row < group.rows; ++row) {
            for (uint32_t column = 0; column < group.columns; ++column) {
                const uint32_t source_index = row * group.columns + column;
                if (area.tileset->group_tile_ids[static_cast<size_t>(group.tile_offset + source_index)]
                    != clicked.id) {
                    continue;
                }
                PlacedGroupFootprint candidate{
                    .group_index = group_index,
                };
                if (!placed_group_matches(area, group, row, column,
                        clicked_index, candidate)) {
                    continue;
                }
                candidate.group_index = group_index;
                if (found && !same_footprint(output, candidate)) {
                    diagnostic = "The placed tileset group is ambiguous";
                    return false;
                }
                if (!found) {
                    output = candidate;
                    found = true;
                }
            }
        }
    }
    if (!found) {
        diagnostic = "The placed tileset group is incomplete";
    }
    return found;
}

bool footprint_contains(const Area& area,
    const PlacedGroupFootprint& footprint,
    uint32_t tile_index) noexcept
{
    const int32_t x = static_cast<int32_t>(
        tile_index % static_cast<uint32_t>(area.width));
    const int32_t y = static_cast<int32_t>(
        tile_index / static_cast<uint32_t>(area.width));
    return x >= footprint.anchor_x && y >= footprint.anchor_y
        && x < footprint.anchor_x + static_cast<int32_t>(footprint.columns)
        && y < footprint.anchor_y + static_cast<int32_t>(footprint.rows);
}

bool resolve_placed_group_containing(const Area& area,
    uint32_t clicked_index,
    PlacedGroupFootprint& output,
    std::string& diagnostic)
{
    bool found = false;
    for (uint32_t tile_index = 0; tile_index < area.tiles.size();
        ++tile_index) {
        const auto& tile = area.tiles[tile_index];
        const bool grouped = tile.id >= 0
            && static_cast<size_t>(tile.id)
                < area.tileset->grouped_tiles.size()
            && area.tileset->grouped_tiles[static_cast<size_t>(tile.id)] != 0;
        if (!grouped) {
            continue;
        }

        PlacedGroupFootprint candidate;
        std::string candidate_diagnostic;
        if (!resolve_placed_group(
                area, tile_index, candidate, candidate_diagnostic)
            || !footprint_contains(area, candidate, clicked_index)) {
            continue;
        }
        if (found && !same_footprint(output, candidate)) {
            diagnostic = "The placed tileset group is ambiguous";
            return false;
        }
        if (!found) {
            output = candidate;
            found = true;
        }
    }
    return found;
}

bool expand_group_replacement_cells(const Area& area,
    std::span<const uint32_t> input,
    std::vector<uint32_t>& output,
    std::string& diagnostic)
{
    output.assign(input.begin(), input.end());
    std::vector<uint8_t> included(area.tiles.size(), 0);
    std::vector<uint8_t> resolved_group_cells(area.tiles.size(), 0);
    for (const uint32_t tile_index : input) {
        if (tile_index >= included.size() || included[tile_index] != 0) {
            diagnostic = "Group replacement cells must be unique and in range";
            return false;
        }
        included[tile_index] = 1;
    }

    for (size_t cursor = 0; cursor < output.size(); ++cursor) {
        const uint32_t tile_index = output[cursor];
        const auto& tile = area.tiles[tile_index];
        const bool group_tile = tile.id >= 0
            && static_cast<size_t>(tile.id)
                < area.tileset->grouped_tiles.size()
            && area.tileset->grouped_tiles[static_cast<size_t>(tile.id)] != 0;
        if (resolved_group_cells[tile_index] != 0) {
            continue;
        }

        PlacedGroupFootprint footprint;
        const bool resolved = group_tile
            ? resolve_placed_group(area, tile_index, footprint, diagnostic)
            : resolve_placed_group_containing(
                  area, tile_index, footprint, diagnostic);
        if (!resolved && !group_tile && diagnostic.empty()) {
            continue;
        }
        if (!resolved) {
            return false;
        }
        for (uint32_t row = 0; row < footprint.rows; ++row) {
            for (uint32_t column = 0; column < footprint.columns; ++column) {
                const uint32_t area_index = static_cast<uint32_t>(
                    (footprint.anchor_y + static_cast<int32_t>(row))
                        * area.width
                    + footprint.anchor_x + static_cast<int32_t>(column));
                if (included[area_index] == 0) {
                    included[area_index] = 1;
                    output.push_back(area_index);
                }
            }
        }
        const auto& group = area.tileset->groups[footprint.group_index];
        for (uint32_t row = 0; row < group.rows; ++row) {
            for (uint32_t column = 0; column < group.columns; ++column) {
                const uint32_t source_index = row * group.columns + column;
                if (area.tileset->group_tile_ids[static_cast<size_t>(group.tile_offset + source_index)] < 0) {
                    continue;
                }
                const auto offset = rotate_group_cell(
                    group, row, column, footprint.orientation);
                const uint32_t area_index = static_cast<uint32_t>(
                    (footprint.anchor_y + static_cast<int32_t>(offset.y)) * area.width
                    + footprint.anchor_x + static_cast<int32_t>(offset.x));
                resolved_group_cells[area_index] = 1;
            }
        }
    }

    return true;
}

void build_placed_group_cell_mask(
    const Area& area, std::vector<uint8_t>& output)
{
    output.assign(area.tiles.size(), 0);
    for (uint32_t tile_index = 0; tile_index < area.tiles.size();
        ++tile_index) {
        const auto& tile = area.tiles[tile_index];
        const bool grouped = tile.id >= 0
            && static_cast<size_t>(tile.id)
                < area.tileset->grouped_tiles.size()
            && area.tileset->grouped_tiles[static_cast<size_t>(tile.id)] != 0;
        if (!grouped || output[tile_index] != 0) {
            continue;
        }

        output[tile_index] = 1;
        PlacedGroupFootprint footprint;
        std::string diagnostic;
        if (!resolve_placed_group(area, tile_index, footprint, diagnostic)) {
            continue;
        }
        for (uint32_t row = 0; row < footprint.rows; ++row) {
            for (uint32_t column = 0; column < footprint.columns; ++column) {
                const uint32_t area_index = static_cast<uint32_t>(
                    (footprint.anchor_y + static_cast<int32_t>(row))
                        * area.width
                    + footprint.anchor_x + static_cast<int32_t>(column));
                output[area_index] = 1;
            }
        }
    }
}

bool overlay_group(const Area& area,
    uint32_t anchor_index,
    int32_t group_index,
    int32_t orientation,
    AreaTopology& topology,
    std::vector<uint8_t>& affected,
    std::vector<uint8_t>& fit_flags,
    std::vector<TileChoice>& fixed,
    std::string& diagnostic)
{
    if (group_index < 0 || orientation < 0 || orientation >= 4
        || static_cast<size_t>(group_index) >= area.tileset->groups.size()) {
        diagnostic = "Tileset group is unavailable";
        return false;
    }
    const auto& group
        = area.tileset->groups[static_cast<size_t>(group_index)];
    const int32_t anchor_x = static_cast<int32_t>(
        anchor_index % static_cast<uint32_t>(area.width));
    const int32_t anchor_y = static_cast<int32_t>(
        anchor_index / static_cast<uint32_t>(area.width));
    const uint64_t group_cell_count
        = static_cast<uint64_t>(group.rows) * group.columns;
    const uint32_t output_columns
        = orientation % 2 == 0 ? group.columns : group.rows;
    const uint32_t output_rows
        = orientation % 2 == 0 ? group.rows : group.columns;
    if (group_cell_count != group.tile_count
        || output_columns > static_cast<uint32_t>(area.width - anchor_x)
        || output_rows > static_cast<uint32_t>(area.height - anchor_y)
        || static_cast<uint64_t>(group.tile_offset) + group.tile_count
            > area.tileset->group_tile_ids.size()) {
        diagnostic = "Tileset group does not fit at the selected cell";
        return false;
    }

    const PlacedGroupFootprint incoming{
        .anchor_x = anchor_x,
        .anchor_y = anchor_y,
        .columns = output_columns,
        .rows = output_rows,
    };
    std::vector<uint32_t> incoming_cells;
    incoming_cells.reserve(static_cast<size_t>(group_cell_count));
    for (uint32_t row = 0; row < incoming.rows; ++row) {
        for (uint32_t column = 0; column < incoming.columns; ++column) {
            incoming_cells.push_back(static_cast<uint32_t>(
                (anchor_y + static_cast<int32_t>(row)) * area.width
                + anchor_x + static_cast<int32_t>(column)));
        }
    }
    std::vector<uint32_t> uncovered_cells;
    if (!expand_group_replacement_cells(area, incoming_cells,
            uncovered_cells, diagnostic)) {
        return false;
    }
    for (const uint32_t tile_index : uncovered_cells) {
        fit_flags[tile_index] |= fit_force | fit_replace_group | fit_canonical;
    }
    std::erase_if(uncovered_cells, [&](uint32_t tile_index) {
        return footprint_contains(area, incoming, tile_index);
    });
    if (!uncovered_cells.empty()
        && !set_terrain_cells(area, uncovered_cells,
            area.tileset->default_terrain, topology, affected, fit_flags)) {
        diagnostic = "The SET default terrain is unavailable for group replacement";
        return false;
    }
    // The incoming group's random-fit cells retain seeded fitting. Only the
    // uncovered part of replaced groups uses canonical eraser ground.
    for (const uint32_t tile_index : incoming_cells) {
        fit_flags[tile_index] &= static_cast<uint8_t>(~fit_canonical);
    }

    AreaTopology overlay{
        .width = topology.width,
        .height = topology.height,
        .corner_terrains = std::vector<int32_t>(
            topology.corner_terrains.size(), unset_value),
        .corner_heights = std::vector<int64_t>(
            topology.corner_heights.size(), unset_height),
        .horizontal_crossers = std::vector<int32_t>(
            topology.horizontal_crossers.size(), unset_value),
        .vertical_crossers = std::vector<int32_t>(
            topology.vertical_crossers.size(), unset_value),
    };
    const int32_t base_height = area.tiles[anchor_index].height;
    for (uint32_t row = 0; row < group.rows; ++row) {
        for (uint32_t column = 0; column < group.columns; ++column) {
            const uint32_t group_cell = row * group.columns + column;
            const int32_t tile_id = area.tileset->group_tile_ids[static_cast<size_t>(group.tile_offset + group_cell)];
            const auto output_cell
                = rotate_group_cell(group, row, column, orientation);
            const int32_t x
                = anchor_x + static_cast<int32_t>(output_cell.x);
            const int32_t y
                = anchor_y + static_cast<int32_t>(output_cell.y);
            const uint32_t area_index = static_cast<uint32_t>(
                y * area.width + x);
            fit_flags[area_index] |= fit_force;
            mark_cell(topology, x, y, affected);
            if (tile_id < 0) {
                continue;
            }
            if (static_cast<size_t>(tile_id)
                    >= area.tileset->tile_topologies.size()
                || !area.tileset->tile_topologies[static_cast<size_t>(tile_id)]
                    .valid
                || !merge_tile_topology(overlay, x, y,
                    area.tileset->tile_topologies[static_cast<size_t>(tile_id)],
                    orientation, base_height)) {
                diagnostic = "Tileset group has inconsistent topology";
                return false;
            }
            fixed[area_index] = {
                .id = tile_id,
                .orientation = orientation,
                .height = base_height,
            };
        }
    }

    for (size_t index = 0; index < overlay.corner_terrains.size(); ++index) {
        if (overlay.corner_terrains[index] != unset_value) {
            topology.corner_terrains[index] = overlay.corner_terrains[index];
            topology.corner_heights[index] = overlay.corner_heights[index];
            const int32_t x = static_cast<int32_t>(
                index % static_cast<size_t>(topology.width + 1));
            const int32_t y = static_cast<int32_t>(
                index / static_cast<size_t>(topology.width + 1));
            mark_corner_incident(topology, x, y, affected);
        }
    }
    for (size_t index = 0; index < overlay.horizontal_crossers.size(); ++index) {
        if (overlay.horizontal_crossers[index] != unset_value) {
            topology.horizontal_crossers[index]
                = overlay.horizontal_crossers[index];
            const int32_t x = static_cast<int32_t>(
                index % static_cast<size_t>(topology.width));
            const int32_t y = static_cast<int32_t>(
                index / static_cast<size_t>(topology.width));
            mark_horizontal_edge_incident(topology, x, y, affected);
        }
    }
    for (size_t index = 0; index < overlay.vertical_crossers.size(); ++index) {
        if (overlay.vertical_crossers[index] != unset_value) {
            topology.vertical_crossers[index]
                = overlay.vertical_crossers[index];
            const int32_t x = static_cast<int32_t>(
                index % static_cast<size_t>(topology.width + 1));
            const int32_t y = static_cast<int32_t>(
                index / static_cast<size_t>(topology.width + 1));
            mark_vertical_edge_incident(topology, x, y, affected);
        }
    }
    return true;
}

ObjectEditApplyResult fit_area_tile_edits(ObjectHandle area_handle,
    const Area& area,
    const AreaTopology& topology,
    std::span<const uint8_t> affected,
    std::span<const uint8_t> fit_flags,
    std::span<const TileChoice> fixed,
    uint64_t seed,
    AreaTileEditBatch& output)
{
    if (affected.size() != area.tiles.size()
        || fit_flags.size() != area.tiles.size()
        || fixed.size() != area.tiles.size()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile fitting buffers do not match the area");
    }

    output.area = area_handle;
    output.rows.reserve(area.tiles.size());
    uint64_t random_state = seed;
    for (uint32_t tile_index = 0; tile_index < area.tiles.size(); ++tile_index) {
        if (affected[tile_index] == 0) {
            continue;
        }
        if (area_tile_is_void(area.tiles[tile_index])
            && fit_flags[tile_index] == 0) {
            continue;
        }
        TileChoice choice = fixed[tile_index];
        if (choice.id >= 0) {
            const int32_t x = static_cast<int32_t>(
                tile_index % static_cast<uint32_t>(area.width));
            const int32_t y = static_cast<int32_t>(
                tile_index / static_cast<uint32_t>(area.width));
            TileChoice checked;
            if (!choice_matches(*area.tileset,
                    choice.id, choice.orientation,
                    cell_terrains(topology, x, y),
                    cell_heights(topology, x, y),
                    cell_crossers(topology, x, y), checked)
                || checked.height != choice.height) {
                output = {};
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Tileset group does not fit the surrounding terrain");
            }
        } else {
            const auto& current = area.tiles[tile_index];
            const bool current_is_group_tile
                = current.id >= 0
                && static_cast<size_t>(current.id)
                    < area.tileset->grouped_tiles.size()
                && area.tileset->grouped_tiles[static_cast<size_t>(current.id)]
                    != 0;
            const uint8_t flags = fit_flags[tile_index];
            if (current_is_group_tile
                && (flags & fit_replace_group) == 0) {
                const int32_t x = static_cast<int32_t>(
                    tile_index % static_cast<uint32_t>(area.width));
                const int32_t y = static_cast<int32_t>(
                    tile_index / static_cast<uint32_t>(area.width));
                if ((flags & fit_force) != 0
                    || !choice_matches(*area.tileset,
                        current.id, current.orientation,
                        cell_terrains(topology, x, y),
                        cell_heights(topology, x, y),
                        cell_crossers(topology, x, y), choice)
                    || choice.height != current.height) {
                    output = {};
                    return brush_result(ObjectEditStatus::invalid_batch,
                        "The brush would split a placed tileset group at cell "
                            + std::to_string(tile_index));
                }
            } else {
                const TileFitMode mode = (flags & fit_canonical) != 0
                    ? TileFitMode::canonical
                    : (flags & fit_force) != 0
                    ? TileFitMode::random_other
                    : TileFitMode::preserve_current;
                if (!fit_cell(area, topology, tile_index,
                        mode, random_state, choice)) {
                    output = {};
                    return brush_result(ObjectEditStatus::invalid_batch,
                        "No SET tile fits the painted terrain at cell "
                            + std::to_string(tile_index));
                }
            }
        }

        auto after = area.tiles[tile_index];
        after.id = choice.id;
        after.orientation = choice.orientation;
        after.height = choice.height;
        if (!area_tile_rows_equal(area.tiles[tile_index], after)) {
            output.rows.push_back({
                .tile_index = tile_index,
                .before = area.tiles[tile_index],
                .after = after,
            });
        }
    }
    if (output.rows.empty()) {
        output = {};
        return brush_result(ObjectEditStatus::empty,
            "Area tile brush contains no changes");
    }
    return {
        .status = ObjectEditStatus::success,
        .applied_count = static_cast<uint32_t>(output.rows.size()),
    };
}

bool append_area_tile_target_doors(const Area& area,
    std::span<const uint32_t> targets,
    AreaTileEraseEditBatch& output,
    std::string& diagnostic)
{
    if (area.doors.empty()) {
        return true;
    }
    AreaDoorHookSnapshot hooks;
    if (!build_area_door_hooks(area, hooks, diagnostic)) {
        return false;
    }
    for (const auto& row : output.tiles.rows) {
        if (!std::ranges::binary_search(targets, row.tile_index)) {
            continue;
        }
        for (uint32_t index = hooks.tile_offsets[row.tile_index];
            index < hooks.tile_offsets[row.tile_index + 1]; ++index) {
            if (hooks.hooks[index].occupant.type == ObjectType::door) {
                output.doors.push_back(hooks.hooks[index].occupant);
            }
        }
    }
    std::ranges::sort(output.doors);
    output.doors.erase(
        std::unique(output.doors.begin(), output.doors.end()),
        output.doors.end());
    return true;
}

} // namespace

ObjectEditApplyResult resolve_area_tile_erase_cells(ObjectHandle area_handle,
    std::span<const uint32_t> input,
    std::vector<uint32_t>& output)
{
    output.clear();
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset || area->width <= 0 || area->height <= 0
        || input.empty()
        || static_cast<uint64_t>(area->width) * area->height != area->tiles.size()
        || area->tiles.size() > std::numeric_limits<uint32_t>::max()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile eraser input is unavailable");
    }
    try {
        std::string diagnostic;
        if (!expand_group_replacement_cells(*area, input, output, diagnostic)) {
            output.clear();
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        std::ranges::sort(output);
        return {
            .status = ObjectEditStatus::success,
            .applied_count = static_cast<uint32_t>(output.size()),
        };
    } catch (const std::bad_alloc&) {
        output.clear();
        return brush_result(ObjectEditStatus::failed,
            "Area tile eraser target allocation failed");
    } catch (const std::length_error&) {
        output.clear();
        return brush_result(ObjectEditStatus::failed,
            "Area tile eraser targets exceed container capacity");
    }
}

ObjectEditApplyResult build_area_tile_erase_edits(ObjectHandle area_handle,
    std::span<const uint32_t> cells, int32_t terrain, uint64_t seed,
    AreaTileEraseEditBatch& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area eraser target is invalid or stale");
    }
    try {
        AreaTileEraseEditBatch candidate;
        std::vector<uint32_t> targets;
        auto resolved = resolve_area_tile_erase_cells(area_handle, cells, targets);
        if (!resolved.ok()) { return resolved; }
        const auto built = build_area_tile_brush_edits(area_handle, targets,
            {.kind = AreaTileBrushKind::eraser, .value = terrain},
            seed, candidate.tiles);
        if (!built.ok()) {
            return built.status == ObjectEditStatus::empty
                ? brush_result(ObjectEditStatus::empty, "Nothing to erase here")
                : built;
        }

        std::string diagnostic;
        if (!append_area_tile_target_doors(
                *area, targets, candidate, diagnostic)) {
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        const auto validated = area_tile_edit_detail::validate_area_tile_edits(
            candidate.tiles, ObjectEditDirection::forward, candidate.doors);
        if (!validated.ok()) { return validated; }
        output = std::move(candidate);
        return {
            .status = ObjectEditStatus::success,
            .applied_count = static_cast<uint32_t>(
                output.tiles.rows.size() + output.doors.size()),
        };
    } catch (const std::bad_alloc&) {
        return brush_result(ObjectEditStatus::failed, "Area eraser allocation failed");
    } catch (const std::length_error&) {
        return brush_result(ObjectEditStatus::failed, "Area eraser exceeds container capacity");
    }
}

ObjectEditApplyResult build_area_tile_void_edits(ObjectHandle area_handle,
    std::span<const uint32_t> cells,
    AreaTileEraseEditBatch& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area void target is invalid or stale");
    }
    try {
        AreaTileEraseEditBatch candidate;
        candidate.tiles.area = area_handle;
        std::vector<uint32_t> targets;
        auto resolved
            = resolve_area_tile_erase_cells(area_handle, cells, targets);
        if (!resolved.ok()) {
            return resolved;
        }
        candidate.tiles.rows.reserve(targets.size());
        for (const uint32_t tile_index : targets) {
            const auto& current = area->tiles[tile_index];
            if (area_tile_is_void(current)) {
                continue;
            }
            AreaTile after{
                .id = kAreaTileVoidId,
                .height = current.height,
            };
            candidate.tiles.rows.push_back({
                .tile_index = tile_index,
                .before = current,
                .after = after,
            });
        }
        if (candidate.tiles.rows.empty()) {
            return brush_result(ObjectEditStatus::empty,
                "Nothing to void here");
        }

        std::string diagnostic;
        if (!append_area_tile_target_doors(
                *area, targets, candidate, diagnostic)) {
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        const auto validated = area_tile_edit_detail::validate_area_tile_edits(
            candidate.tiles, ObjectEditDirection::forward, candidate.doors);
        if (!validated.ok()) {
            return validated;
        }
        output = std::move(candidate);
        return {
            .status = ObjectEditStatus::success,
            .applied_count = static_cast<uint32_t>(
                output.tiles.rows.size() + output.doors.size()),
        };
    } catch (const std::bad_alloc&) {
        return brush_result(ObjectEditStatus::failed,
            "Area void allocation failed");
    } catch (const std::length_error&) {
        return brush_result(ObjectEditStatus::failed,
            "Area void exceeds container capacity");
    }
}

ObjectEditApplyResult build_area_tile_selection(
    ObjectHandle area_handle,
    uint32_t source_tile_index,
    AreaTileSelection& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset || area->width <= 0 || area->height <= 0
        || source_tile_index >= area->tiles.size()
        || static_cast<uint64_t>(area->width)
                * static_cast<uint64_t>(area->height)
            != area->tiles.size()
        || area->tiles.size() > std::numeric_limits<uint32_t>::max()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile selection input is unavailable");
    }

    try {
        output.area = area_handle;
        output.source_tile_index = source_tile_index;
        const auto& source = area->tiles[source_tile_index];
        const bool grouped_source = source.id >= 0
            && static_cast<size_t>(source.id)
                < area->tileset->grouped_tiles.size()
            && area->tileset->grouped_tiles[static_cast<size_t>(source.id)]
                != 0;
        PlacedGroupFootprint footprint;
        std::string diagnostic;
        const bool grouped = grouped_source
            ? resolve_placed_group(
                  *area, source_tile_index, footprint, diagnostic)
            : resolve_placed_group_containing(
                  *area, source_tile_index, footprint, diagnostic);
        if (!grouped && diagnostic.empty() && !grouped_source) {
            output.tile_indices.push_back(source_tile_index);
            return {
                .status = ObjectEditStatus::success,
                .applied_count = 1,
            };
        }

        if (!grouped) {
            output = {};
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        const uint64_t selection_count
            = static_cast<uint64_t>(footprint.columns) * footprint.rows;
        if (selection_count == 0
            || selection_count > area->tiles.size()) {
            output = {};
            return brush_result(ObjectEditStatus::invalid_batch,
                "The placed tileset group footprint is invalid");
        }
        output.group_index = footprint.group_index;
        output.tile_indices.reserve(static_cast<size_t>(selection_count));
        for (uint32_t row = 0; row < footprint.rows; ++row) {
            for (uint32_t column = 0; column < footprint.columns; ++column) {
                output.tile_indices.push_back(static_cast<uint32_t>(
                    (footprint.anchor_y + static_cast<int32_t>(row))
                        * area->width
                    + footprint.anchor_x + static_cast<int32_t>(column)));
            }
        }
        return {
            .status = ObjectEditStatus::success,
            .applied_count = static_cast<uint32_t>(selection_count),
        };
    } catch (const std::bad_alloc&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area tile selection allocation failed");
    } catch (const std::length_error&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area tile selection exceeds container capacity");
    }
}

ObjectEditApplyResult build_area_tile_brush_edits(
    ObjectHandle area_handle,
    std::span<const uint32_t> ordered_tile_indices,
    AreaTileBrush brush,
    uint64_t seed,
    AreaTileEditBatch& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset || ordered_tile_indices.empty()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile brush input is unavailable");
    }
    if (area->tiles.size() > std::numeric_limits<uint32_t>::max()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile grid exceeds the supported index range");
    }
    if ((brush.kind != AreaTileBrushKind::group
            && brush.orientation != 0)
        || brush.orientation < 0 || brush.orientation >= 4) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile brush orientation is invalid");
    }

    try {
        std::vector<uint8_t> fit_flags(area->tiles.size(), 0);
        for (const uint32_t tile_index : ordered_tile_indices) {
            if (tile_index >= area->tiles.size()
                || fit_flags[tile_index] != 0) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Area tile brush cells must be unique and in range");
            }
            fit_flags[tile_index] = fit_force;
        }

        AreaTopology topology;
        std::string diagnostic;
        if (!decode_area_topology(*area, topology, diagnostic)) {
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        std::vector<uint8_t> affected(area->tiles.size(), 0);
        std::vector<TileChoice> fixed(area->tiles.size());
        std::vector<uint32_t> eraser_cells;

        switch (brush.kind) {
        case AreaTileBrushKind::terrain:
            if (!set_terrain_cells(*area, ordered_tile_indices, brush.value,
                    topology, affected, fit_flags)) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Terrain brush is outside the SET terrain catalog");
            }
            break;
        case AreaTileBrushKind::crosser:
            if (!set_crosser_path(*area, ordered_tile_indices, brush.value,
                    topology, affected, fit_flags)) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Drag a crosser brush across at least two area tiles");
            }
            break;
        case AreaTileBrushKind::group:
            if (ordered_tile_indices.size() != 1
                || !overlay_group(*area, ordered_tile_indices.front(),
                    brush.value, brush.orientation, topology, affected,
                    fit_flags,
                    fixed, diagnostic)) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    diagnostic.empty()
                        ? "Place a tileset feature with one click"
                        : std::move(diagnostic));
            }
            break;
        case AreaTileBrushKind::eraser:
            if (!expand_group_replacement_cells(*area, ordered_tile_indices,
                    eraser_cells, diagnostic)) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    std::move(diagnostic));
            }
            if (!set_eraser_cells(*area, eraser_cells, brush.value,
                    topology, affected, fit_flags)) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Eraser terrain is outside the SET terrain catalog");
            }
            break;
        case AreaTileBrushKind::void_tile:
            return brush_result(ObjectEditStatus::invalid_batch,
                "Void tiles require the structural tile edit path");
        case AreaTileBrushKind::raise:
        case AreaTileBrushKind::lower:
            return brush_result(ObjectEditStatus::invalid_batch,
                "Raise / Lower requires grid-corner targets");
        }

        return fit_area_tile_edits(area_handle, *area, topology,
            affected, fit_flags, fixed, seed, output);
    } catch (const std::bad_alloc&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area tile brush allocation failed");
    } catch (const std::length_error&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area tile brush exceeds container capacity");
    }
}

ObjectEditApplyResult build_area_tile_variation_edits(
    ObjectHandle area_handle,
    std::span<const uint32_t> ordered_tile_indices,
    AreaTileEditBatch& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset || ordered_tile_indices.empty()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile variation input is unavailable");
    }
    if (area->tiles.size() > std::numeric_limits<uint32_t>::max()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile grid exceeds the supported index range");
    }

    try {
        std::vector<uint8_t> visited(area->tiles.size(), 0);
        for (const uint32_t tile_index : ordered_tile_indices) {
            if (tile_index >= area->tiles.size()
                || visited[tile_index] != 0) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Area tile variation cells must be unique and in range");
            }
            visited[tile_index] = 1;
        }

        AreaTopology topology;
        std::string diagnostic;
        if (!decode_area_topology(*area, topology, diagnostic)) {
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        output.area = area_handle;
        output.rows.reserve(ordered_tile_indices.size());
        for (uint32_t tile_index = 0; tile_index < area->tiles.size();
            ++tile_index) {
            if (visited[tile_index] == 0) {
                continue;
            }
            const auto& current = area->tiles[tile_index];
            if (current.id >= 0
                && static_cast<size_t>(current.id)
                    < area->tileset->grouped_tiles.size()
                && area->tileset->grouped_tiles[static_cast<size_t>(current.id)]
                    != 0) {
                output = {};
                return brush_result(ObjectEditStatus::invalid_batch,
                    "A placed tileset group cannot be varied one tile at a time");
            }
            TileChoice choice;
            if (!next_cell_variation(
                    *area, topology, tile_index, choice)) {
                continue;
            }
            auto after = current;
            after.id = choice.id;
            after.orientation = choice.orientation;
            after.height = choice.height;
            output.rows.push_back({
                .tile_index = tile_index,
                .before = current,
                .after = after,
            });
        }
        if (output.rows.empty()) {
            output = {};
            return brush_result(ObjectEditStatus::empty,
                "The selected tile has no other compatible variation");
        }
        return {
            .status = ObjectEditStatus::success,
            .applied_count = static_cast<uint32_t>(output.rows.size()),
        };
    } catch (const std::bad_alloc&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area tile variation allocation failed");
    } catch (const std::length_error&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area tile variation exceeds container capacity");
    }
}

ObjectEditApplyResult build_area_tile_height_brush_edits(
    ObjectHandle area_handle,
    std::span<const uint32_t> ordered_corner_indices,
    int32_t delta,
    uint64_t seed,
    AreaTileEditBatch& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset || ordered_corner_indices.empty()
        || (delta != -1 && delta != 1)) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area height brush input is unavailable");
    }
    if (area->tiles.size() > std::numeric_limits<uint32_t>::max()) {
        return brush_result(ObjectEditStatus::invalid_batch,
            "Area tile grid exceeds the supported index range");
    }

    try {
        AreaTopology topology;
        std::string diagnostic;
        if (!decode_area_topology(*area, topology, diagnostic)) {
            return brush_result(ObjectEditStatus::invalid_batch,
                std::move(diagnostic));
        }
        std::vector<uint8_t> visited(topology.corner_heights.size(), 0);
        for (const uint32_t corner_index : ordered_corner_indices) {
            if (corner_index >= visited.size()
                || visited[corner_index] != 0) {
                return brush_result(ObjectEditStatus::invalid_batch,
                    "Area height brush corners must be unique and in range");
            }
            visited[corner_index] = 1;
        }

        std::vector<uint8_t> affected(area->tiles.size(), 0);
        std::vector<uint8_t> fit_flags(area->tiles.size(), 0);
        std::vector<TileChoice> fixed(area->tiles.size());
        if (!change_corner_heights(
                *area, ordered_corner_indices, delta, topology, affected)) {
            return brush_result(ObjectEditStatus::invalid_batch,
                "This SET does not support the requested terrain height");
        }
        return fit_area_tile_edits(area_handle, *area, topology,
            affected, fit_flags, fixed, seed, output);
    } catch (const std::bad_alloc&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area height brush allocation failed");
    } catch (const std::length_error&) {
        output = {};
        return brush_result(ObjectEditStatus::failed,
            "Area height brush exceeds container capacity");
    }
}

} // namespace nw::toolset
