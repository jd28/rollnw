#pragma once

#include "command_bus.hpp"
#include "object_edits.hpp"

#include <nw/objects/Area.hpp>

#include <cstdint>
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

[[nodiscard]] bool area_tile_rows_equal(
    const AreaTile& lhs, const AreaTile& rhs) noexcept;

ObjectEditApplyResult apply_area_tile_edits(
    const AreaTileEditBatch& batch, ObjectEditDirection direction);

CommandResult commit_area_tile_edits(
    AreaTileEditBatch batch, std::string label, CommandContext& context);

} // namespace nw::toolset
