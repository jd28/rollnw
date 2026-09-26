#pragma once

#include <nw/objects/Area.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace nw::toolset {

enum class AreaTilePointerButton : uint8_t {
    other,
    primary,
    secondary,
};

enum class AreaTilePointerModifier : uint8_t {
    none,
    select,
    blocked,
};

enum class AreaTilePointerAction : uint8_t {
    none,
    paint,
    select,
    cycle_variation,
};

struct AreaTilePointerInput {
    AreaTilePointerButton button = AreaTilePointerButton::other;
    AreaTilePointerModifier modifier = AreaTilePointerModifier::none;
    bool secondary_paint_available = false;
};

struct AreaTilePointerResult {
    AreaTilePointerAction action = AreaTilePointerAction::none;
    bool consumed = false;
};

// Batch contract: borrowed contiguous inputs and outputs correspond by index
// for the duration of the call. Cost is O(input count) time and O(1) memory.
// Mismatched spans clear every output. Unknown enum values produce no action;
// an unknown modifier consumes a recognized pointer button so it cannot paint.
void resolve_area_tile_pointer_actions(
    std::span<const AreaTilePointerInput> inputs,
    std::span<AreaTilePointerResult> outputs) noexcept;

[[nodiscard]] AreaTilePointerAction resolve_area_tile_pointer_action(
    AreaTilePointerInput input) noexcept;

[[nodiscard]] AreaTilePointerResult resolve_area_tile_pointer_input(
    AreaTilePointerInput input) noexcept;

enum class AreaTileCellPickStatus : uint8_t {
    hit,
    miss,
    invalid_input,
};

struct AreaTileCellRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f};
};

struct AreaTileCellPick {
    glm::vec3 position{0.0f};
    float distance = 0.0f;
    uint32_t tile_index = UINT32_MAX;
    AreaTileCellPickStatus status = AreaTileCellPickStatus::invalid_input;
};

enum class AreaTileEdgeAxis : uint8_t {
    horizontal,
    vertical,
    invalid,
};

// Canonical area edge-lattice coordinate. Horizontal edges accept
// x in [0, width) and y in [0, height]; vertical edges accept
// x in [0, width] and y in [0, height). Boundary coordinates are valid.
struct AreaTileCrosserEdge {
    uint32_t x = 0;
    uint32_t y = 0;
    AreaTileEdgeAxis axis = AreaTileEdgeAxis::invalid;

    bool operator==(const AreaTileCrosserEdge&) const = default;
};

// One canonical crosser pointer target. The cell drives drag interpolation and
// the edge drives topology edits and preview geometry; both are resolved from
// the same world hit position.
struct AreaTileCrosserTarget {
    uint32_t tile_index = UINT32_MAX;
    AreaTileCrosserEdge edge;
};

// World-space centerline occupied by a crosser as it passes through one
// canonical edge. The line is perpendicular to the targeted boundary and is
// clipped to the area at perimeter edges.
struct AreaTileCrosserSpan {
    glm::vec2 start{0.0f};
    glm::vec2 end{0.0f};
    bool valid = false;
};

// Inputs and outputs correspond by index. Mismatched spans or invalid area
// dimensions clear every output; invalid edges produce invalid rows.
void resolve_area_tile_crosser_spans(
    int32_t width,
    int32_t height,
    std::span<const AreaTileCrosserEdge> edges,
    std::span<AreaTileCrosserSpan> output) noexcept;

[[nodiscard]] AreaTileCrosserSpan resolve_area_tile_crosser_span(
    int32_t width,
    int32_t height,
    AreaTileCrosserEdge edge) noexcept;

// Batch fallback for logical cells without selectable rendered geometry.
// Inputs and outputs correspond by index. The area is borrowed for the call.
void pick_area_tile_cells(
    const Area& area,
    std::span<const AreaTileCellRay> rays,
    std::span<AreaTileCellPick> output) noexcept;

[[nodiscard]] AreaTileCellPick pick_area_tile_cell(
    const Area& area, const AreaTileCellRay& ray) noexcept;

// Resolves rendered hits through their world positions instead of the owning
// model rows. Inputs and outputs correspond by index. Positions past the area
// clamp to its perimeter; invalid inputs produce invalid targets.
void pick_area_tile_crosser_targets(
    const Area& area,
    std::span<const AreaTileCellPick> cell_hits,
    std::span<AreaTileCrosserTarget> output) noexcept;

[[nodiscard]] AreaTileCrosserTarget pick_area_tile_crosser_target(
    const Area& area, const AreaTileCellPick& cell_hit) noexcept;

// Converts cell hits to the nearest canonical edge-lattice coordinate. The
// rendered world position determines the logical cell through the same target
// protocol used by dragging. Invalid inputs produce invalid edges.
void pick_area_tile_crosser_edges(
    const Area& area,
    std::span<const AreaTileCellPick> cell_hits,
    std::span<AreaTileCrosserEdge> output) noexcept;

[[nodiscard]] AreaTileCrosserEdge pick_area_tile_crosser_edge(
    const Area& area, const AreaTileCellPick& cell_hit) noexcept;

// Converts rendered cell hits to their nearest row-major corner-lattice index.
// Outputs are UINT32_MAX when the corresponding hit or area is invalid.
void pick_area_tile_corners(
    const Area& area,
    std::span<const AreaTileCellPick> cell_hits,
    std::span<uint32_t> output) noexcept;

[[nodiscard]] uint32_t pick_area_tile_corner(
    const Area& area, const AreaTileCellPick& cell_hit) noexcept;

struct AreaTileCornerCellSet {
    std::array<uint32_t, 4> tile_indices{};
    uint8_t count = 0;
};

// Resolves each corner to the at-most-four incident row-major area tile cells.
// Invalid inputs produce an empty corresponding set.
void resolve_area_tile_corner_cells(
    int32_t width,
    int32_t height,
    std::span<const uint32_t> corner_indices,
    std::span<AreaTileCornerCellSet> output) noexcept;

[[nodiscard]] AreaTileCornerCellSet resolve_area_tile_corner_cell(
    int32_t width, int32_t height, uint32_t corner_index) noexcept;

enum class AreaTileLineStatus : uint8_t {
    success,
    invalid_input,
    failed,
};

struct AreaTileCellCoord {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const AreaTileCellCoord&) const = default;
};

struct AreaTileLineResult {
    AreaTileLineStatus status = AreaTileLineStatus::invalid_input;
    uint32_t visited_count = 0;
    uint32_t appended_count = 0;
};

// Appends the grid cells crossed by the segment between two cell centers.
// `visited` has one byte per row and coalesces repeated gesture visits.
AreaTileLineResult append_area_tile_grid_line(
    int32_t width,
    int32_t height,
    AreaTileCellCoord from,
    AreaTileCellCoord to,
    std::span<uint8_t> visited,
    std::vector<uint32_t>& indices) noexcept;

// Appends the canonical edge batch crossed by a segment between two cell
// centers. The visited buffer has width * (height + 1) horizontal bytes
// followed by (width + 1) * height vertical bytes. Existing edges coalesce.
AreaTileLineResult append_area_tile_crosser_line(
    int32_t width,
    int32_t height,
    AreaTileCellCoord from,
    AreaTileCellCoord to,
    std::span<uint8_t> visited,
    std::vector<AreaTileCrosserEdge>& edges) noexcept;

// Appends a borrowed edge batch through the same canonical visited mask used
// by crosser lines. Invalid input writes nothing.
AreaTileLineResult append_area_tile_crosser_edges(
    int32_t width,
    int32_t height,
    std::span<const AreaTileCrosserEdge> input,
    std::span<uint8_t> visited,
    std::vector<AreaTileCrosserEdge>& edges) noexcept;

} // namespace nw::toolset
