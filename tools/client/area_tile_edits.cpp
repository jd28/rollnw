#include "area_tile_edits.hpp"

#include "area_door_hooks.hpp"
#include "workspace.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace nw::toolset {
namespace {

ObjectEditApplyResult tile_result(
    ObjectEditStatus status, std::string diagnostic = {})
{
    return {
        .status = status,
        .diagnostic = std::move(diagnostic),
    };
}

CommandResult tile_command_result(
    CommandStatus status,
    std::string message,
    CommandOutputChannel channel)
{
    return {
        .status = status,
        .message = std::move(message),
        .output_channel = channel,
    };
}

struct DirectedAreaTileRow {
    const AreaTile* expected = nullptr;
    const AreaTile* replacement = nullptr;
};

DirectedAreaTileRow directed_row(
    const AreaTileEditRow& row, ObjectEditDirection direction) noexcept
{
    return direction == ObjectEditDirection::forward
        ? DirectedAreaTileRow{
              .expected = &row.before,
              .replacement = &row.after,
          }
        : DirectedAreaTileRow{
              .expected = &row.after,
              .replacement = &row.before,
          };
}

bool valid_area_shape(const Area& area, uint32_t& tile_count) noexcept
{
    if (area.width <= 0 || area.height <= 0
        || area.width > std::numeric_limits<int16_t>::max()
        || area.height > std::numeric_limits<int16_t>::max()) {
        return false;
    }
    const uint64_t count = static_cast<uint64_t>(area.width)
        * static_cast<uint64_t>(area.height);
    if (count > std::numeric_limits<uint32_t>::max()
        || count != area.tiles.size()) {
        return false;
    }
    tile_count = static_cast<uint32_t>(count);
    return true;
}

bool valid_tile_row(const Area& area, const AreaTile& tile) noexcept
{
    if (!area.tileset) {
        return false;
    }
    const float world_z = static_cast<float>(tile.height)
        * area.tileset->tile_height;
    if (!std::isfinite(world_z)) {
        return false;
    }
    if (area_tile_is_void(tile)) {
        return tile.orientation == 0
            && tile.animloop1 == 0 && tile.animloop2 == 0
            && tile.animloop3 == 0 && tile.mainlight1 == 0
            && tile.mainlight2 == 0 && tile.srclight1 == 0
            && tile.srclight2 == 0;
    }
    return tile.id >= 0
        && static_cast<size_t>(tile.id) < area.tileset->tiles.size()
        && tile.orientation >= 0 && tile.orientation < 4;
}

bool valid_tile_light_value(AreaTileLightSlot slot, uint8_t value) noexcept
{
    switch (slot) {
    case AreaTileLightSlot::main1:
    case AreaTileLightSlot::main2:
        return value <= 31;
    case AreaTileLightSlot::source1:
    case AreaTileLightSlot::source2:
        return value <= 15;
    case AreaTileLightSlot::invalid:
        return false;
    }
    return false;
}

void set_tile_light_value(
    AreaTile& tile, AreaTileLightSlot slot, uint8_t value) noexcept
{
    switch (slot) {
    case AreaTileLightSlot::main1:
        tile.mainlight1 = value;
        break;
    case AreaTileLightSlot::main2:
        tile.mainlight2 = value;
        break;
    case AreaTileLightSlot::source1:
        tile.srclight1 = value;
        break;
    case AreaTileLightSlot::source2:
        tile.srclight2 = value;
        break;
    case AreaTileLightSlot::invalid:
        break;
    }
}

bool row_changes_spatial_tile_data(const AreaTileEditRow& row) noexcept
{
    return row.before.id != row.after.id
        || row.before.height != row.after.height
        || row.before.orientation != row.after.orientation;
}

ObjectEditApplyResult validate_occupied_hooks(
    const Area& area,
    const AreaTileEditBatch& batch,
    ObjectEditDirection direction,
    std::span<const ObjectHandle> removed_doors)
{
    const auto has_door_hooks = [&area](const AreaTile& tile) {
        return !area_tile_is_void(tile)
            && tile.id >= 0
            && static_cast<size_t>(tile.id) < area.tileset->tiles.size()
            && !area.tileset->tiles[static_cast<size_t>(tile.id)]
                    .door_slots.empty();
    };
    const auto has_affected_door_hooks
        = [&has_door_hooks](const AreaTileEditRow& row) {
              return row_changes_spatial_tile_data(row)
                  && (has_door_hooks(row.before) || has_door_hooks(row.after));
          };
    if (std::none_of(batch.rows.begin(), batch.rows.end(),
            has_affected_door_hooks)) {
        return tile_result(ObjectEditStatus::success);
    }

    AreaDoorHookSnapshot current_hooks;
    std::string diagnostic;
    if (!build_area_door_hooks(area, area.tiles, current_hooks, diagnostic, removed_doors)) {
        return tile_result(ObjectEditStatus::invalid_batch,
            diagnostic.empty()
                ? "Area door hooks are unavailable"
                : std::move(diagnostic));
    }
    if (current_hooks.tile_offsets.size() != area.tiles.size() + 1u) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Area door hook offsets do not match the tile grid");
    }

    Vector<AreaTile> candidate_tiles;
    try {
        candidate_tiles = area.tiles;
        for (const auto& edit : batch.rows) {
            const auto directed = directed_row(edit, direction);
            candidate_tiles[edit.tile_index] = *directed.replacement;
        }
    } catch (const std::bad_alloc&) {
        return tile_result(ObjectEditStatus::failed,
            "Candidate area tile allocation failed");
    } catch (const std::length_error&) {
        return tile_result(ObjectEditStatus::failed,
            "Candidate area tile array exceeds container capacity");
    }

    AreaDoorHookSnapshot candidate_hooks;
    if (!build_area_door_hooks(
            area, candidate_tiles, candidate_hooks, diagnostic, removed_doors)) {
        return tile_result(ObjectEditStatus::invalid_batch,
            diagnostic.empty()
                ? "Candidate area door hooks are unavailable"
                : std::move(diagnostic));
    }
    if (candidate_hooks.tile_offsets.size() != area.tiles.size() + 1u) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Candidate area door hook offsets do not match the tile grid");
    }

    const AreaDoorHookSnapshot* snapshots[]{&current_hooks, &candidate_hooks};
    for (const auto* hooks : snapshots) {
        for (const auto& edit : batch.rows) {
            if (!row_changes_spatial_tile_data(edit)) {
                continue;
            }
            const uint32_t first = hooks->tile_offsets[edit.tile_index];
            const uint32_t last = hooks->tile_offsets[edit.tile_index + 1u];
            if (first > last || last > hooks->hooks.size()) {
                return tile_result(ObjectEditStatus::invalid_batch,
                    "Area door hook offsets are invalid");
            }
            for (uint32_t index = first; index < last; ++index) {
                if (hooks->hooks[index].occupant.type != ObjectType::invalid) {
                    return tile_result(ObjectEditStatus::invalid_batch,
                        "Tile " + std::to_string(edit.tile_index)
                            + " has an occupied door hook");
                }
            }
        }
    }
    return tile_result(ObjectEditStatus::success);
}

} // namespace

ObjectEditApplyResult area_tile_edit_detail::validate_area_tile_edits(
    const AreaTileEditBatch& batch, ObjectEditDirection direction,
    std::span<const ObjectHandle> removed_doors)
{
    if (batch.rows.empty()) {
        return tile_result(ObjectEditStatus::empty,
            "Area tile edit batch is empty");
    }
    if (batch.area.type != ObjectType::area) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Area tile edit target is invalid");
    }
    const auto* area = kernel::objects().get<Area>(batch.area);
    if (!area || !area->tileset || !std::isfinite(area->tileset->tile_height)
        || area->tileset->tile_height <= 0.0f) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Area tile edit target or tileset is invalid or stale");
    }

    uint32_t tile_count = 0;
    if (!valid_area_shape(*area, tile_count)) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Area dimensions do not match its tile rows");
    }
    if (batch.rows.size() > tile_count) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Area tile edit contains more rows than the area");
    }

    for (size_t index = 0; index < batch.rows.size(); ++index) {
        const auto& edit = batch.rows[index];
        if (edit.tile_index >= tile_count
            || (index > 0
                && batch.rows[index - 1].tile_index >= edit.tile_index)) {
            return tile_result(ObjectEditStatus::invalid_batch,
                "Area tile edit indices must be sorted, unique, and in range");
        }
        const auto directed = directed_row(edit, direction);
        if (!area_tile_rows_equal(
                area->tiles[edit.tile_index], *directed.expected)) {
            return tile_result(ObjectEditStatus::stale_value,
                "Area tile " + std::to_string(edit.tile_index)
                    + " changed before the edit was applied");
        }
        if (!valid_tile_row(*area, edit.before)
            || !valid_tile_row(*area, edit.after)) {
            return tile_result(ObjectEditStatus::invalid_batch,
                "Area tile " + std::to_string(edit.tile_index)
                    + " snapshot is outside the tileset or transform range");
        }
    }
    return validate_occupied_hooks(*area, batch, direction, removed_doors);
}

namespace {

void mark_context_dirty(CommandContext& context)
{
    if (context.workspace && !context.active_tab_id.empty()) {
        (void)context.workspace->set_tab_dirty(context.active_tab_id, true);
    }
}

CommandResult replay_area_tile_edits(
    const AreaTileEditBatch& batch,
    ObjectEditDirection direction,
    std::string label,
    CommandContext& context)
{
    const auto applied = apply_area_tile_edits(batch, direction);
    if (!applied.ok()) {
        return tile_command_result(CommandStatus::failed,
            applied.diagnostic.empty()
                ? "Area tile edit replay failed"
                : applied.diagnostic,
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return tile_command_result(CommandStatus::success,
        std::move(label), CommandOutputChannel::none);
}

} // namespace

bool area_tile_rows_equal(
    const AreaTile& lhs, const AreaTile& rhs) noexcept
{
    return lhs.id == rhs.id
        && lhs.height == rhs.height
        && lhs.orientation == rhs.orientation
        && lhs.animloop1 == rhs.animloop1
        && lhs.animloop2 == rhs.animloop2
        && lhs.animloop3 == rhs.animloop3
        && lhs.mainlight1 == rhs.mainlight1
        && lhs.mainlight2 == rhs.mainlight2
        && lhs.srclight1 == rhs.srclight1
        && lhs.srclight2 == rhs.srclight2;
}

ObjectEditApplyResult build_area_tile_light_edits(
    ObjectHandle area_handle,
    std::span<const uint32_t> tile_indices,
    AreaTileLightSlot slot,
    uint8_t value,
    AreaTileEditBatch& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    uint32_t tile_count = 0;
    if (!area || !valid_area_shape(*area, tile_count)
        || !valid_tile_light_value(slot, value)
        || tile_indices.empty()) {
        return tile_result(ObjectEditStatus::invalid_batch,
            "Area tile light input is unavailable or out of range");
    }

    try {
        AreaTileEditBatch candidate;
        candidate.area = area_handle;
        candidate.rows.reserve(tile_indices.size());
        for (size_t index = 0; index < tile_indices.size(); ++index) {
            const uint32_t tile_index = tile_indices[index];
            if (tile_index >= tile_count
                || (index > 0 && tile_indices[index - 1] >= tile_index)) {
                return tile_result(ObjectEditStatus::invalid_batch,
                    "Area tile light indices must be sorted, unique, and in range");
            }
            AreaTile replacement = area->tiles[tile_index];
            set_tile_light_value(replacement, slot, value);
            if (!area_tile_rows_equal(area->tiles[tile_index], replacement)) {
                candidate.rows.push_back({
                    .tile_index = tile_index,
                    .before = area->tiles[tile_index],
                    .after = replacement,
                });
            }
        }
        if (candidate.rows.empty()) {
            return tile_result(ObjectEditStatus::empty,
                "Area tile lights already use that color");
        }
        const auto validated = area_tile_edit_detail::validate_area_tile_edits(
            candidate, ObjectEditDirection::forward);
        if (!validated.ok()) {
            return validated;
        }
        output = std::move(candidate);
        return {
            .status = ObjectEditStatus::success,
            .applied_count = static_cast<uint32_t>(output.rows.size()),
        };
    } catch (const std::bad_alloc&) {
        return tile_result(ObjectEditStatus::failed,
            "Area tile light allocation failed");
    } catch (const std::length_error&) {
        return tile_result(ObjectEditStatus::failed,
            "Area tile light batch exceeds container capacity");
    }
}

ObjectEditApplyResult apply_area_tile_edits(
    const AreaTileEditBatch& batch, ObjectEditDirection direction)
{
    auto validation = area_tile_edit_detail::validate_area_tile_edits(batch, direction);
    if (!validation.ok()) {
        return validation;
    }

    const uint32_t changed_count = area_tile_edit_detail::write_area_tile_edits(batch, direction);
    if (changed_count == 0) {
        return tile_result(ObjectEditStatus::empty,
            "Area tile edit contains no changes");
    }

    publish_area_tile_changes(batch.area, batch.rows);
    return {
        .status = ObjectEditStatus::success,
        .applied_count = changed_count,
    };
}

uint32_t area_tile_edit_detail::write_area_tile_edits(
    const AreaTileEditBatch& batch, ObjectEditDirection direction)
{
    auto* area = kernel::objects().get<Area>(batch.area);
    uint32_t changed_count = 0;
    for (const auto& edit : batch.rows) {
        const auto directed = directed_row(edit, direction);
        if (!area_tile_rows_equal(*directed.expected, *directed.replacement)) {
            area->tiles[edit.tile_index] = *directed.replacement;
            ++changed_count;
        }
    }
    return changed_count;
}

CommandResult commit_area_tile_edits(
    AreaTileEditBatch batch, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* tab = context.workspace->active_tab();
        if (!tab || tab->kind != WorkspaceTabKind::area
            || tab->document.object() != batch.area) {
            return tile_command_result(CommandStatus::rejected,
                "Tile editing is only available for the displayed area",
                CommandOutputChannel::warn);
        }
    }

    std::shared_ptr<AreaTileEditBatch> shared_batch;
    std::shared_ptr<CommandUndoAction> action;
    try {
        shared_batch = std::make_shared<AreaTileEditBatch>(std::move(batch));
        action = std::make_shared<CommandUndoAction>();
        action->label = label;
        const std::string undo_label = "Undo " + label;
        const std::string redo_label = "Redo " + label;
        action->undo = [shared_batch, undo_label](CommandContext& undo_context) {
            return replay_area_tile_edits(*shared_batch,
                ObjectEditDirection::inverse,
                undo_label,
                undo_context);
        };
        action->redo = [shared_batch, redo_label](CommandContext& redo_context) {
            return replay_area_tile_edits(*shared_batch,
                ObjectEditDirection::forward,
                redo_label,
                redo_context);
        };
    } catch (const std::bad_alloc&) {
        return tile_command_result(CommandStatus::failed,
            "Area tile undo allocation failed",
            CommandOutputChannel::error);
    } catch (const std::length_error&) {
        return tile_command_result(CommandStatus::failed,
            "Area tile undo exceeds container capacity",
            CommandOutputChannel::error);
    }

    const auto applied = apply_area_tile_edits(
        *shared_batch, ObjectEditDirection::forward);
    if (!applied.ok()) {
        const bool failed = applied.status == ObjectEditStatus::failed;
        const bool empty = applied.status == ObjectEditStatus::empty;
        return tile_command_result(
            empty        ? CommandStatus::noop
                : failed ? CommandStatus::failed
                         : CommandStatus::rejected,
            applied.diagnostic.empty()
                ? "Area tile edit was rejected"
                : applied.diagnostic,
            empty        ? CommandOutputChannel::none
                : failed ? CommandOutputChannel::error
                         : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    auto result = tile_command_result(CommandStatus::success,
        std::move(label), CommandOutputChannel::none);
    result.undo_action = std::move(action);
    return result;
}

} // namespace nw::toolset
