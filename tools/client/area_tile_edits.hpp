#pragma once

#include "command_bus.hpp"
#include "object_edits.hpp"

#include <nw/objects/Area.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nw::toolset {

struct AreaTileEditRow {
    uint32_t tile_index = 0;
    AreaTile before;
    AreaTile after;
};

struct AreaTileEditBatch {
    ObjectHandle area{};
    std::vector<AreaTileEditRow> rows;
};

struct AreaTileEraseEditBatch {
    AreaTileEditBatch tiles;
    std::vector<ObjectHandle> doors;
};

enum class AreaTileLightSlot : uint8_t {
    main1,
    main2,
    source1,
    source2,
    invalid,
};

// Local command preflight/write boundary. Validation borrows doors that the
// same erase operation will detach; other occupied hooks still reject. Writes
// require successful validation on this UI thread with no intervening edits,
// perform no allocation, and leave publication to the command coordinator.
namespace area_tile_edit_detail {
ObjectEditApplyResult validate_area_tile_edits(const AreaTileEditBatch& batch,
    ObjectEditDirection direction, std::span<const ObjectHandle> removed_doors = {});
uint32_t write_area_tile_edits(const AreaTileEditBatch& batch,
    ObjectEditDirection direction);
}

[[nodiscard]] bool area_tile_rows_equal(
    const AreaTile& lhs, const AreaTile& rhs) noexcept;

// Builds one atomic row-major batch for a tile selection. Main-light values
// are NWN constants [0, 31]; source-light values are [0, 15]. Inputs must be
// sorted and unique. Invalid input clears output and writes no live rows.
[[nodiscard]] ObjectEditApplyResult build_area_tile_light_edits(
    ObjectHandle area,
    std::span<const uint32_t> tile_indices,
    AreaTileLightSlot slot,
    uint8_t value,
    AreaTileEditBatch& output);

ObjectEditApplyResult apply_area_tile_edits(
    const AreaTileEditBatch& batch, ObjectEditDirection direction);

CommandResult commit_area_tile_edits(
    AreaTileEditBatch batch, std::string label, CommandContext& context);

} // namespace nw::toolset
