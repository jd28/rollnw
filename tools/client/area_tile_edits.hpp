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

ObjectEditApplyResult apply_area_tile_edits(
    const AreaTileEditBatch& batch, ObjectEditDirection direction);

CommandResult commit_area_tile_edits(
    AreaTileEditBatch batch, std::string label, CommandContext& context);

} // namespace nw::toolset
