#pragma once

#include "area_tile_edits.hpp"

#include <nw/objects/ObjectHandle.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace nw::toolset {

struct AreaTileCrosserEdge;

enum class AreaTileBrushKind : uint8_t {
    terrain,
    crosser,
    group,
    eraser,
    void_tile,
    raise,
    lower,
};

struct AreaTileBrush {
    AreaTileBrushKind kind = AreaTileBrushKind::terrain;
    int32_t value = -1;
    int32_t orientation = 0;
};

// One pointer pick resolves to either one ordinary area cell or the complete
// rectangular footprint of a verified placed SET group. Tile indices are
// row-major, owned by this value, and valid only while the area's tile grid
// and tileset resource remain unchanged.
struct AreaTileSelection {
    ObjectHandle area{};
    std::vector<uint32_t> tile_indices;
    uint32_t source_tile_index = UINT32_MAX;
    uint32_t group_index = UINT32_MAX;

    [[nodiscard]] bool active() const noexcept
    {
        return area.type == ObjectType::area && !tile_indices.empty();
    }

    [[nodiscard]] bool is_group() const noexcept
    {
        return group_index != UINT32_MAX;
    }
};

// Expands a borrowed batch of unique area indices into sorted, owned eraser
// targets, including every cell of groups intersected by the picked cells.
// Neighboring groups are fixed boundaries and are never added implicitly. Does
// not fit or change tiles. Invalid indices, incomplete/ambiguous groups, and
// allocation failures return an empty output. Input and output storage must not
// alias. Scratch memory is O(area cells); work includes existing SET group
// resolution.
[[nodiscard]] ObjectEditApplyResult resolve_area_tile_erase_cells(
    ObjectHandle area,
    std::span<const uint32_t> input,
    std::vector<uint32_t>& output);

// Builds an owned erase batch from unique picked cells. Expands only intersected
// groups, preserves neighboring group boundaries while fitting ordinary
// transition tiles, and includes doors attached to changed target tiles. Empty
// or invalid input clears the complete output.
[[nodiscard]] ObjectEditApplyResult build_area_tile_erase_edits(ObjectHandle area,
    std::span<const uint32_t> cells, int32_t terrain, uint64_t seed,
    AreaTileEraseEditBatch& output);

// Builds one atomic void batch from unique picked cells. Intersected SET
// groups are expanded to their complete footprint, occupied doors are removed,
// and every resulting row keeps its editing-plane height with id == -1.
[[nodiscard]] ObjectEditApplyResult build_area_tile_void_edits(
    ObjectHandle area,
    std::span<const uint32_t> cells,
    AreaTileEraseEditBatch& output);

// Resolves one picked cell into a complete authoring selection. Incomplete or
// ambiguous placed groups are rejected atomically; output is empty on failure.
[[nodiscard]] ObjectEditApplyResult build_area_tile_selection(
    ObjectHandle area,
    uint32_t source_tile_index,
    AreaTileSelection& output);

// Builds one atomic batch from the ordered tile cells visited by a terrain,
// crosser, or group gesture. The seed controls variant choice; the chosen
// AreaTile rows are stored in undo. Height brushes are rejected here because
// they target the area's corner lattice.
[[nodiscard]] ObjectEditApplyResult build_area_tile_brush_edits(
    ObjectHandle area,
    std::span<const uint32_t> ordered_tile_indices,
    AreaTileBrush brush,
    uint64_t seed,
    AreaTileEditBatch& output,
    std::span<const AreaTileCrosserEdge> crosser_edges = {});

// Cycles each placed, non-group tile to the next SET row with identical world
// topology. Inputs must be unique row-major area cell indices. The result is
// one atomic batch; cells without another variation are omitted.
[[nodiscard]] ObjectEditApplyResult build_area_tile_variation_edits(
    ObjectHandle area,
    std::span<const uint32_t> ordered_tile_indices,
    AreaTileEditBatch& output);

// Builds one atomic height batch from unique row-major corner-lattice indices.
// Delta must be -1 or 1. Only tiles incident to changed corners are fitted.
// The batch is rejected if fitting would vertically split a placed SET group.
[[nodiscard]] ObjectEditApplyResult build_area_tile_height_brush_edits(
    ObjectHandle area,
    std::span<const uint32_t> ordered_corner_indices,
    int32_t delta,
    uint64_t seed,
    AreaTileEditBatch& output);

} // namespace nw::toolset
