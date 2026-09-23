#include "area_door_hooks.hpp"
#include "area_tile_editor.hpp"
#include "command_view.hpp"
#include "renderer.hpp"
#include "shell_controller.hpp"
#include "toolset_backend.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace nw::toolset {

namespace {

bool area_tile_removal_brush(AreaTileBrushKind kind) noexcept
{
    return kind == AreaTileBrushKind::eraser
        || kind == AreaTileBrushKind::void_tile;
}

std::string_view area_tile_light_slot_label(
    AreaTileLightSlot slot) noexcept
{
    switch (slot) {
    case AreaTileLightSlot::main1:
        return "Main Light 1";
    case AreaTileLightSlot::main2:
        return "Main Light 2";
    case AreaTileLightSlot::source1:
        return "Source Light 1";
    case AreaTileLightSlot::source2:
        return "Source Light 2";
    case AreaTileLightSlot::invalid:
        return "Tile Light";
    }
    return "Tile Light";
}

bool preview_area_tile_stroke(ClientRenderer& renderer,
    AreaTileEditorState& editor,
    nw::ObjectHandle area,
    std::span<const uint32_t> tile_indices,
    std::span<const uint32_t> corner_indices,
    nw::toolset::AreaTileBrush brush)
{
    nw::toolset::AreaTileEditBatch preview;
    nw::toolset::AreaTileEraseEditBatch removal_preview;
    const bool erasing = brush.kind == nw::toolset::AreaTileBrushKind::eraser;
    const bool voiding
        = brush.kind == nw::toolset::AreaTileBrushKind::void_tile;
    const bool height_brush
        = brush.kind == nw::toolset::AreaTileBrushKind::raise
        || brush.kind == nw::toolset::AreaTileBrushKind::lower;
    const std::optional<uint32_t> anchor_tile_index
        = !height_brush && !tile_indices.empty()
        ? std::optional<uint32_t>{tile_indices.back()}
        : std::nullopt;
    auto built = erasing
        ? build_area_tile_erase_edits(area, tile_indices, brush.value,
              editor.next_random_seed, removal_preview)
        : voiding
        ? build_area_tile_void_edits(
              area, tile_indices, removal_preview)
        : build_area_tile_stroke_edits(area, tile_indices, corner_indices,
              brush, editor.next_random_seed, preview);
    if (built.ok() && !area_tile_removal_brush(brush.kind)) {
        built = area_tile_edit_detail::validate_area_tile_edits(
            preview, ObjectEditDirection::forward);
    }
    editor.feedback = built.ok() ? std::string{} : built.diagnostic;
    if (!built.ok()) {
        try {
            editor.preview_rows.clear();
            editor.preview_rows.reserve(tile_indices.size());
            for (const uint32_t tile_index : tile_indices) {
                editor.preview_rows.push_back({.tile_index = tile_index});
            }
        } catch (const std::bad_alloc&) {
            editor.preview_rows.clear();
        } catch (const std::length_error&) {
            editor.preview_rows.clear();
        }
        (void)renderer.update_viewer_area_tile_preview(
            area, editor.preview_rows, false, false, anchor_tile_index);
        return false;
    }
    try {
        editor.preview_rows.clear();
        std::vector<uint32_t> erase_cells;
        const auto erase_targets = erasing
            ? nw::toolset::resolve_area_tile_erase_cells(area, tile_indices, erase_cells)
            : nw::toolset::ObjectEditApplyResult{.status = ObjectEditStatus::success};
        if (erasing && !erase_targets.ok()) {
            editor.feedback = erase_targets.diagnostic;
            (void)renderer.update_viewer_area_tile_preview(area, {}, false);
            return false;
        }
        if (erasing && erase_targets.ok()) {
            const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
            editor.preview_rows.reserve(erase_cells.size());
            for (const uint32_t tile_index : erase_cells) {
                const auto& tile = live_area->tiles[tile_index];
                editor.preview_rows.push_back({
                    .tile_index = tile_index,
                    .tile_id = tile.id,
                    .height = tile.height,
                    .orientation = tile.orientation,
                });
            }
        } else {
            const auto& rows
                = voiding ? removal_preview.tiles.rows : preview.rows;
            editor.preview_rows.reserve(rows.size());
            for (const auto& row : rows) {
                editor.preview_rows.push_back({
                    .tile_index = row.tile_index,
                    .tile_id = row.after.id,
                    .height = row.after.height,
                    .orientation = row.after.orientation,
                });
            }
        }
    } catch (const std::bad_alloc&) {
        editor.feedback = "Tile preview allocation failed";
        (void)renderer.update_viewer_area_tile_preview(area, {}, false);
        return false;
    } catch (const std::length_error&) {
        editor.feedback = "Tile preview exceeds container capacity";
        (void)renderer.update_viewer_area_tile_preview(area, {}, false);
        return false;
    }
    (void)renderer.update_viewer_area_tile_preview(
        area, editor.preview_rows, true, !erasing, anchor_tile_index);
    return true;
}

struct AreaEraseDoorTilePick {
    std::optional<uint32_t> tile_index;
    std::string diagnostic;
};

AreaEraseDoorTilePick pick_area_erase_door_tile(ClientRenderer& renderer,
    const Area& area, Rml::Vector2f point, ClientViewportRect viewport)
{
    std::vector<ObjectHandle> doors;
    doors.reserve(area.doors.size());
    for (const auto* door : area.doors) {
        if (door) { doors.push_back(door->handle()); }
    }
    const auto hit = renderer.viewer_area_door_hit(point.x, point.y, viewport, doors);
    if (!hit) { return {}; }
    AreaDoorHookSnapshot hooks;
    AreaEraseDoorTilePick result;
    if (!build_area_door_hooks(area, hooks, result.diagnostic)) {
        if (result.diagnostic.empty()) {
            result.diagnostic = "Area door hooks are unavailable";
        }
        return result;
    }
    result.tile_index = area_door_hook_tile(hooks, hit->door);
    if (!result.tile_index) {
        result.diagnostic = "The door is not attached to one area tile hook";
    }
    return result;
}

nw::toolset::AreaTileCellPick pick_area_tile_cell(
    ClientRenderer& renderer,
    nw::ObjectHandle area_handle,
    Rml::Vector2f point,
    ClientViewportRect viewport)
{
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area || area->width <= 0 || area->height <= 0) {
        return {};
    }

    // One pointer event is a true singleton. Prefer the rendered model owner
    // so tall and overhanging tiles select the geometry under the pointer.
    // The authored cell planes remain the fallback for void or missing models.
    const uint64_t tile_count = static_cast<uint64_t>(area->width)
        * static_cast<uint64_t>(area->height);
    if (tile_count == area->tiles.size()
        && tile_count <= std::numeric_limits<uint32_t>::max()) {
        const auto hit = renderer.viewer_area_tile_hit(
            point.x, point.y, viewport);
        if (hit && hit->tile_x >= 0 && hit->tile_y >= 0
            && hit->tile_x < area->width && hit->tile_y < area->height
            && std::isfinite(hit->position.x)
            && std::isfinite(hit->position.y)
            && std::isfinite(hit->position.z)
            && std::isfinite(hit->distance) && hit->distance >= 0.0f) {
            return {
                .position = hit->position,
                .distance = hit->distance,
                .tile_index = static_cast<uint32_t>(hit->tile_y)
                        * static_cast<uint32_t>(area->width)
                    + static_cast<uint32_t>(hit->tile_x),
                .status = nw::toolset::AreaTileCellPickStatus::hit,
            };
        }
    }

    const auto ray = renderer.viewer_viewport_ray(
        point.x, point.y, viewport);
    return ray
        ? nw::toolset::pick_area_tile_cell(*area,
              {
                  .origin = ray->origin,
                  .direction = ray->displacement,
              })
        : nw::toolset::AreaTileCellPick{};
}

bool update_area_tile_outlines(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, const AreaTileSelection& selection)
{
    const auto* area = nw::kernel::objects().get<nw::Area>(selection.area);
    if (!selection.active() || selection.area != active_area
        || !area || !area->tileset
        || std::ranges::any_of(selection.tile_indices,
            [area](uint32_t tile_index) {
                return tile_index >= area->tiles.size();
            })) {
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_area, {});
        return false;
    }

    try {
        editor.preview_rows.clear();
        editor.preview_rows.reserve(selection.tile_indices.size());
        for (const uint32_t tile_index : selection.tile_indices) {
            const auto& tile = area->tiles[tile_index];
            editor.preview_rows.push_back({
                .tile_index = tile_index,
                .tile_id = tile.id,
                .height = tile.height,
                .orientation = tile.orientation,
            });
        }
    } catch (const std::bad_alloc&) {
        editor.feedback = "Tile selection highlight allocation failed";
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            selection.area, {});
        return false;
    } catch (const std::length_error&) {
        editor.feedback = "Tile selection highlight exceeds container capacity";
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            selection.area, {});
        return false;
    }

    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    if (!renderer.update_viewer_area_tile_preview(
            selection.area, editor.preview_rows, true, false,
            selection.source_tile_index)) {
        editor.feedback = "Tile selection highlight is unavailable";
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            selection.area, {});
        return false;
    }
    return true;
}

bool set_area_tile_selection(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, uint32_t tile_index, ShellController& shell)
{
    const ObjectHandle area = active_area;
    cancel_area_tile_stroke(renderer, editor, active_area);
    editor.selection = {};
    editor.light_editor_slot = AreaTileLightSlot::invalid;

    nw::toolset::AreaTileSelection selection;
    const auto built = nw::toolset::build_area_tile_selection(
        area, tile_index, selection);
    if (!built.ok()) {
        editor.feedback = built.diagnostic;
        shell.append_output(
            built.status == nw::toolset::ObjectEditStatus::failed
                ? "error"
                : "warn",
            built.diagnostic);
        return false;
    }
    editor.selection = std::move(selection);
    editor.feedback.clear();
    (void)update_area_tile_selection_preview(renderer, editor, active_area);
    return editor.selection.active();
}

} // namespace

AreaTileLightClickEffect apply_area_tile_light_click(
    AreaTileLightClick& click,
    AreaTileEditorState& editor,
    ObjectHandle active_area,
    bool actions_allowed,
    ToolsetBackend& backend,
    const CommandContext& context,
    ShellController& shell)
{
    const auto kind
        = std::exchange(click.kind, AreaTileLightClickKind::none);
    if (kind == AreaTileLightClickKind::none
        || kind > AreaTileLightClickKind::select_color
        || !editor.selection.active()
        || editor.selection.area != click.area
        || active_area != click.area
        || !kernel::objects().valid(click.area)
        || object_mutation_state().epoch != click.mutation_epoch
        || editor.selection.source_tile_index != click.source_tile_index
        || editor.selection.group_index != click.group_index
        || editor.light_editor_slot != click.open_slot
        || click.slot == AreaTileLightSlot::invalid) {
        return AreaTileLightClickEffect::none;
    }

    if (kind == AreaTileLightClickKind::toggle_slot) {
        editor.light_editor_slot = editor.light_editor_slot == click.slot
            ? AreaTileLightSlot::invalid
            : click.slot;
        editor.feedback.clear();
        return AreaTileLightClickEffect::presentation_changed;
    }

    if (!actions_allowed || click.open_slot != click.slot) {
        editor.feedback = "Tile light editing is unavailable";
        return AreaTileLightClickEffect::unavailable;
    }

    AreaTileEditBatch edit;
    const auto built = build_area_tile_light_edits(click.area,
        editor.selection.tile_indices, click.slot, click.value, edit);
    if (built.status == ObjectEditStatus::empty) {
        editor.feedback.clear();
        return AreaTileLightClickEffect::presentation_changed;
    }
    if (!built.ok()) {
        editor.feedback = built.diagnostic;
        shell.append_output(
            built.status == ObjectEditStatus::failed ? "error" : "warn",
            built.diagnostic);
        return AreaTileLightClickEffect::unavailable;
    }

    std::string label{"Set "};
    label += area_tile_light_slot_label(click.slot);
    const auto result
        = backend.edit_area_tiles(std::move(edit), std::move(label), context);
    editor.feedback = result.ok() ? std::string{} : result.message;
    append_command_results(shell, {&result, 1});
    return result.ok() ? AreaTileLightClickEffect::value_changed
                       : AreaTileLightClickEffect::unavailable;
}

void refresh_area_tile_selection_after_rebuild(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle area)
{
    if (editor.selection.active()) {
        const uint32_t source_tile_index
            = editor.selection
                  .source_tile_index;
        nw::toolset::AreaTileSelection selection;
        const auto selection_result
            = nw::toolset::build_area_tile_selection(
                area, source_tile_index,
                selection);
        if (selection_result.ok()) {
            editor.selection
                = std::move(selection);
            (void)update_area_tile_selection_preview(
                renderer, editor, area);
        } else {
            clear_area_tile_selection(renderer, editor, area);
            editor.feedback
                = "Tile selection cleared: "
                + selection_result.diagnostic;
        }
    } else if (editor.cursor_target_index
        != UINT32_MAX) {
        editor.cursor_target_index
            = UINT32_MAX;
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            area, {});
    }
}

void clear_area_tile_selection(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area)
{
    editor.selection = {};
    editor.light_editor_slot = AreaTileLightSlot::invalid;
    if (editor.stroke.active) {
        return;
    }
    editor.preview_rows.clear();
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_modifier = nw::toolset::AreaTilePointerModifier::none;
    editor.cursor_update_pending = false;
    (void)renderer.update_viewer_area_tile_preview(
        active_area, {});
}

bool update_area_tile_selection_preview(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area)
{
    if (!update_area_tile_outlines(
            renderer, editor, active_area, editor.selection)) {
        clear_area_tile_selection(renderer, editor, active_area);
        return false;
    }
    return true;
}

bool select_area_tiles(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, bool actions_allowed, ShellController& shell)
{
    if (!actions_allowed) {
        return false;
    }
    cancel_area_tile_stroke(renderer, editor, active_area);
    const nw::ObjectHandle area = active_area;
    const auto pick = pick_area_tile_cell(
        renderer, area, point, viewport);
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        editor.feedback
            = "Tile selection target is unavailable";
        return true;
    }
    (void)set_area_tile_selection(
        renderer, editor, active_area, pick.tile_index, shell);
    return true;
}

bool cycle_area_tile_at_point(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, bool actions_allowed, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!actions_allowed) {
        return false;
    }
    const nw::ObjectHandle area = active_area;
    const auto pick = pick_area_tile_cell(
        renderer, area, point, viewport);
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        editor.feedback
            = "Tile variation target is unavailable";
        return true;
    }
    if (set_area_tile_selection(renderer, editor, active_area, pick.tile_index, shell)) {
        (void)cycle_selected_area_tile_variation(renderer, editor, active_area, actions_allowed, backend, context, shell);
    }
    return true;
}

void cancel_area_tile_stroke(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area)
{
    const bool was_active = editor.stroke.active;
    editor.stroke = {};
    if (was_active) {
        (void)SDL_CaptureMouse(false);
    }
    editor.preview_rows.clear();
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_modifier = nw::toolset::AreaTilePointerModifier::none;
    editor.cursor_update_pending = false;
    (void)renderer.update_viewer_area_tile_preview(
        active_area, {});
}

bool cancel_area_tile_action(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area)
{
    const bool had_action = editor.stroke.active || editor.selection.active()
        || editor.selected_row >= 0 || !editor.preview_rows.empty()
        || editor.cursor_update_pending;
    if (!had_action) {
        return false;
    }
    cancel_area_tile_stroke(renderer, editor, active_area);
    editor.selection = {};
    editor.light_editor_slot = AreaTileLightSlot::invalid;
    editor.selected_row = -1;
    editor.group_orientation = 0;
    editor.feedback.clear();
    editor.list.set_selected(-1);
    editor.rendered = false;
    return true;
}

bool update_area_tile_cursor(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, AreaTilePointerModifier modifier, ShellController& shell)
{
    const nw::ObjectHandle area_handle = active_area;
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area) {
        return false;
    }
    if (!editor.stroke.active
        && modifier
            == nw::toolset::AreaTilePointerModifier::select) {
        const auto pick = pick_area_tile_cell(
            renderer, area_handle, point, viewport);
        if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
            editor.cursor_target_index = UINT32_MAX;
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
            return true;
        }
        if (editor.cursor_target_index == pick.tile_index
            && !editor.preview_rows.empty()) {
            return true;
        }
        nw::toolset::AreaTileSelection hovered;
        const auto built = nw::toolset::build_area_tile_selection(
            area_handle, pick.tile_index, hovered);
        if (!built.ok()) {
            editor.feedback = built.diagnostic;
            editor.cursor_target_index = UINT32_MAX;
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
            return true;
        }
        if (update_area_tile_outlines(renderer, editor, active_area, hovered)) {
            editor.cursor_target_index = pick.tile_index;
        }
        return true;
    }
    if (!editor.stroke.active && editor.selection.active()) {
        return true;
    }
    const auto brush = editor.stroke.active
        ? std::optional{editor.stroke.brush}
        : selected_area_tile_brush(editor, AreaTilePointerButton::primary);
    nw::toolset::AreaTileCellPick pick;
    if (brush && area_tile_removal_brush(brush->kind)) {
        try {
            const auto door = pick_area_erase_door_tile(
                renderer, *area, point, viewport);
            if (door.tile_index) {
                pick = {
                    .tile_index = *door.tile_index,
                    .status = nw::toolset::AreaTileCellPickStatus::hit,
                };
            } else if (!door.diagnostic.empty()) {
                editor.cursor_target_index = UINT32_MAX;
                editor.feedback = door.diagnostic;
                editor.preview_rows.clear();
                (void)renderer.update_viewer_area_tile_preview(
                    area_handle, {}, false);
                if (editor.stroke.active) {
                    editor.stroke.has_last_target = false;
                }
                return true;
            }
        } catch (const std::bad_alloc&) {
            editor.feedback = "Door eraser target allocation failed";
            if (editor.stroke.active) {
                shell.append_output("error", editor.feedback);
                cancel_area_tile_stroke(renderer, editor, active_area);
            }
            return true;
        } catch (const std::length_error&) {
            editor.feedback = "Door eraser targets exceed container capacity";
            if (editor.stroke.active) {
                shell.append_output("error", editor.feedback);
                cancel_area_tile_stroke(renderer, editor, active_area);
            }
            return true;
        }
    }
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        pick = pick_area_tile_cell(
            renderer, area_handle, point, viewport);
    }
    if (pick.status != nw::toolset::AreaTileCellPickStatus::hit) {
        editor.cursor_target_index = UINT32_MAX;
        if (editor.stroke.active) {
            editor.feedback = "Tile placement target is outside the area or unavailable";
            editor.stroke.has_last_target = false;
        } else {
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
        }
        return true;
    }

    if (!editor.stroke.active) {
        if (!brush) {
            editor.cursor_target_index = UINT32_MAX;
            editor.preview_rows.clear();
            (void)renderer.update_viewer_area_tile_preview(area_handle, {});
            return true;
        }
        if (area_tile_height_brush(*brush)) {
            const uint32_t corner
                = nw::toolset::pick_area_tile_corner(*area, pick);
            if (corner == UINT32_MAX) {
                editor.feedback = "Terrain height target is unavailable";
                editor.cursor_target_index = UINT32_MAX;
                editor.preview_rows.clear();
                (void)renderer.update_viewer_area_tile_preview(
                    area_handle, {}, false);
                return true;
            }
            if (editor.cursor_target_index == corner
                && !editor.preview_rows.empty()) {
                return true;
            }
            editor.cursor_target_index = corner;
            const auto cells = nw::toolset::resolve_area_tile_corner_cell(
                area->width, area->height, corner);
            const std::array corners{corner};
            (void)preview_area_tile_stroke(renderer, editor, area_handle,
                std::span<const uint32_t>{cells.tile_indices.data(),
                    cells.count},
                corners, *brush);
        } else {
            if (editor.cursor_target_index == pick.tile_index
                && !editor.preview_rows.empty()) {
                return true;
            }
            editor.cursor_target_index = pick.tile_index;
            const std::array cells{pick.tile_index};
            (void)preview_area_tile_stroke(renderer, editor, area_handle,
                cells, {}, *brush);
        }
        return true;
    }

    auto& stroke = editor.stroke;
    if (stroke.brush.kind == nw::toolset::AreaTileBrushKind::group
        && !stroke.tile_indices.empty()) {
        return true;
    }
    const bool height_brush = area_tile_height_brush(stroke.brush);
    const uint32_t target_index = height_brush
        ? nw::toolset::pick_area_tile_corner(*area, pick)
        : pick.tile_index;
    if (target_index == UINT32_MAX) {
        editor.feedback = "Terrain stroke target is unavailable";
        stroke.has_last_target = false;
        return true;
    }
    const int32_t target_width
        = height_brush ? stroke.width + 1 : stroke.width;
    const int32_t target_height
        = height_brush ? stroke.height + 1 : stroke.height;
    const nw::toolset::AreaTileCellCoord target{
        .x = static_cast<int32_t>(
            target_index % static_cast<uint32_t>(target_width)),
        .y = static_cast<int32_t>(
            target_index / static_cast<uint32_t>(target_width)),
    };
    auto& target_indices
        = height_brush ? stroke.corner_indices : stroke.tile_indices;
    const size_t appended_begin = target_indices.size();
    const auto appended = nw::toolset::append_area_tile_grid_line(
        target_width,
        target_height,
        stroke.has_last_target ? stroke.last_target : target,
        target,
        stroke.visited,
        target_indices);
    if (appended.status != nw::toolset::AreaTileLineStatus::success) {
        editor.feedback = "Tile stroke buffer update failed";
        shell.append_output("error", "Tile stroke buffer update failed");
        cancel_area_tile_stroke(renderer, editor, active_area);
        return true;
    }
    if (appended.appended_count == 0) {
        stroke.last_target = target;
        stroke.has_last_target = true;
        return true;
    }
    if (height_brush
        && !append_area_tile_height_preview_cells(stroke,
            std::span<const uint32_t>{target_indices}.subspan(
                appended_begin))) {
        editor.feedback = "Tile stroke preview allocation failed";
        shell.append_output("error", editor.feedback);
        cancel_area_tile_stroke(renderer, editor, active_area);
        return true;
    }
    stroke.last_target = target;
    stroke.has_last_target = true;
    (void)preview_area_tile_stroke(renderer, editor, area_handle,
        stroke.tile_indices, stroke.corner_indices, stroke.brush);
    return true;
}

bool begin_area_tile_stroke(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, uint8_t pointer_button, AreaTileBrush brush, AreaTilePointerModifier modifier, bool actions_allowed, ShellController& shell)
{
    const nw::ObjectHandle area_handle = active_area;
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!actions_allowed || editor.stroke.active || !area
        || !area->tileset || editor.selected_row < 0
        || static_cast<size_t>(editor.selected_row)
            >= editor.palette.rows.size()) {
        editor.feedback = "Terrain editing is unavailable";
        shell.append_output("warn", editor.feedback);
        return false;
    }

    const uint64_t tile_count = static_cast<uint64_t>(area->width)
        * static_cast<uint64_t>(area->height);
    if (area->width <= 0 || area->height <= 0
        || area->width == std::numeric_limits<int32_t>::max()
        || area->height == std::numeric_limits<int32_t>::max()
        || tile_count != area->tiles.size()
        || tile_count > std::numeric_limits<uint32_t>::max()) {
        editor.feedback = "Area tile grid is malformed";
        shell.append_output("error", "Area tile grid is malformed");
        return true;
    }
    const bool height_brush = area_tile_height_brush(brush);
    const uint64_t target_count = height_brush
        ? static_cast<uint64_t>(area->width + 1)
            * static_cast<uint64_t>(area->height + 1)
        : tile_count;
    if (target_count > std::numeric_limits<uint32_t>::max()) {
        editor.feedback = "Area height grid exceeds the supported index range";
        shell.append_output("error", editor.feedback);
        return true;
    }
    const uint32_t displayed_target = editor.cursor_target_index;
    const bool displayed_target_available
        = displayed_target != UINT32_MAX
        && static_cast<uint64_t>(displayed_target) < target_count
        && !editor.preview_rows.empty()
        && modifier == nw::toolset::AreaTilePointerModifier::none
        && editor.cursor_modifier == modifier;
    try {
        const auto& palette_row
            = editor.palette.rows[static_cast<size_t>(editor.selected_row)];
        editor.stroke = {
            .area = area_handle,
            .tileset = area->tileset_resref,
            .brush = brush,
            .label = palette_row.label,
            .mutation_epoch = nw::toolset::object_mutation_state().epoch,
            .resource_generation = nw::kernel::resman().generation(),
            .width = area->width,
            .height = area->height,
            .visited = std::vector<uint8_t>(
                static_cast<size_t>(target_count), 0),
            .previewed_tiles = height_brush
                ? std::vector<uint8_t>(static_cast<size_t>(tile_count), 0)
                : std::vector<uint8_t>{},
            .pointer_button = pointer_button,
            .active = true,
        };
        editor.stroke.tile_indices.reserve(
            static_cast<size_t>(tile_count));
        if (height_brush) {
            editor.stroke.corner_indices.reserve(
                static_cast<size_t>(target_count));
        }
        editor.feedback.clear();
        (void)SDL_CaptureMouse(true);
    } catch (const std::bad_alloc&) {
        editor.stroke = {};
        editor.feedback = "Tile stroke allocation failed";
        shell.append_output("error", "Tile stroke allocation failed");
        return true;
    } catch (const std::length_error&) {
        editor.stroke = {};
        editor.feedback = "Tile stroke exceeds container capacity";
        shell.append_output("error", "Tile stroke exceeds container capacity");
        return true;
    }
    if (displayed_target_available) {
        auto& stroke = editor.stroke;
        const int32_t target_width
            = height_brush ? stroke.width + 1 : stroke.width;
        const int32_t target_height
            = height_brush ? stroke.height + 1 : stroke.height;
        const nw::toolset::AreaTileCellCoord target{
            .x = static_cast<int32_t>(
                displayed_target % static_cast<uint32_t>(target_width)),
            .y = static_cast<int32_t>(
                displayed_target / static_cast<uint32_t>(target_width)),
        };
        auto& target_indices
            = height_brush ? stroke.corner_indices : stroke.tile_indices;
        const auto appended = nw::toolset::append_area_tile_grid_line(
            target_width, target_height, target, target,
            stroke.visited, target_indices);
        if (appended.status
                != nw::toolset::AreaTileLineStatus::success
            || appended.appended_count != 1) {
            editor.feedback = "Tile stroke buffer update failed";
            shell.append_output("error", editor.feedback);
            cancel_area_tile_stroke(renderer, editor, active_area);
            return true;
        }
        if (height_brush
            && !append_area_tile_height_preview_cells(
                stroke, stroke.corner_indices)) {
            editor.feedback = "Tile stroke preview allocation failed";
            shell.append_output("error", editor.feedback);
            cancel_area_tile_stroke(renderer, editor, active_area);
            return true;
        }
        stroke.last_target = target;
        stroke.has_last_target = true;
        (void)preview_area_tile_stroke(renderer, editor, area_handle,
            stroke.tile_indices, stroke.corner_indices, stroke.brush);
    } else {
        (void)update_area_tile_cursor(
            renderer, editor, active_area, point, viewport, modifier, shell);
    }
    if (editor.stroke.active
        && (height_brush ? editor.stroke.corner_indices.empty()
                         : editor.stroke.tile_indices.empty())) {
        shell.append_output("warn", editor.feedback.empty() ? "Tile placement target is unavailable" : editor.feedback);
        cancel_area_tile_stroke(renderer, editor, active_area);
    }
    return true;
}

void commit_area_tile_stroke(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, bool actions_allowed, bool viewport_stale, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!editor.stroke.active) {
        return;
    }
    if (!(actions_allowed && editor.stroke.active && active_area == editor.stroke.area)
        || viewport_stale) {
        cancel_area_tile_stroke(renderer, editor, active_area);
        return;
    }
    auto stroke = std::move(editor.stroke);
    editor.stroke = {};
    (void)SDL_CaptureMouse(false);
    (void)renderer.update_viewer_area_tile_preview(stroke.area, {});
    editor.preview_rows.clear();
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    const auto* area = nw::kernel::objects().get<nw::Area>(stroke.area);
    if (!area || active_area != stroke.area
        || area->tileset_resref != stroke.tileset
        || area->width != stroke.width || area->height != stroke.height
        || nw::kernel::resman().generation() != stroke.resource_generation
        || nw::toolset::object_mutation_state().epoch
            != stroke.mutation_epoch) {
        editor.feedback = "Tile stroke was cancelled because its area changed";
        shell.append_output("warn", "Tile stroke was cancelled because its area changed");
        return;
    }
    nw::toolset::AreaTileEditBatch edit;
    AreaTileEraseEditBatch removal_edit;
    const bool erasing = stroke.brush.kind == AreaTileBrushKind::eraser;
    const bool voiding
        = stroke.brush.kind == AreaTileBrushKind::void_tile;
    const auto built = erasing
        ? build_area_tile_erase_edits(stroke.area, stroke.tile_indices,
              stroke.brush.value, editor.next_random_seed++, removal_edit)
        : voiding
        ? build_area_tile_void_edits(
              stroke.area, stroke.tile_indices, removal_edit)
        : build_area_tile_stroke_edits(stroke.area,
              stroke.tile_indices, stroke.corner_indices, stroke.brush,
              editor.next_random_seed++, edit);
    if (!built.ok()) {
        if (area_tile_removal_brush(stroke.brush.kind)
            && built.status == nw::toolset::ObjectEditStatus::empty) {
            editor.feedback = built.diagnostic;
            return;
        }
        editor.feedback = built.diagnostic;
        (void)preview_area_tile_stroke(renderer, editor, stroke.area,
            stroke.tile_indices, stroke.corner_indices, stroke.brush);
        shell.append_output(
            built.status == nw::toolset::ObjectEditStatus::failed
                ? "error"
                : "warn",
            built.diagnostic);
        return;
    }
    const size_t target_count = area_tile_height_brush(stroke.brush)
        ? stroke.corner_indices.size()
        : stroke.tile_indices.size();
    const auto label = target_count == 1 ? stroke.label : stroke.label + " stroke";
    const auto result = area_tile_removal_brush(stroke.brush.kind)
        ? backend.erase_area_tiles(std::move(removal_edit), label, context)
        : backend.edit_area_tiles(std::move(edit), label, context);
    editor.feedback = result.ok() ? std::string{} : result.message;
    append_command_results(shell, {&result, 1});
}

bool cycle_selected_area_tile_variation(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, bool actions_allowed, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!actions_allowed
        || !editor.selection.active()) {
        return false;
    }
    if (editor.selection.is_group()) {
        editor.feedback
            = "Placed groups do not expose interchangeable group variations";
        shell.append_output("warn", editor.feedback);
        return true;
    }
    cancel_area_tile_stroke(renderer, editor, active_area);
    const nw::ObjectHandle area = editor.selection.area;
    const std::array cells{editor.selection.source_tile_index};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_variation_edits(
        area, cells, edit);
    if (!built.ok()) {
        editor.feedback = built.diagnostic;
        shell.append_output(
            built.status == nw::toolset::ObjectEditStatus::failed
                ? "error"
                : "warn",
            built.diagnostic);
        (void)update_area_tile_selection_preview(renderer, editor, active_area);
        return true;
    }
    const auto result = backend.edit_area_tiles(std::move(edit),
        "Cycle tile variation",
        context);
    editor.feedback = result.ok() ? std::string{} : result.message;
    append_command_results(shell, {&result, 1});
    (void)update_area_tile_selection_preview(renderer, editor, active_area);
    return true;
}

bool prepare_area_tile_cursor_update(ClientRenderer& renderer, AreaTileEditorState& editor,
    ObjectHandle active_area, AreaTilePointerModifier modifier)
{
    if (!editor.stroke.active && modifier != editor.cursor_modifier) {
        editor.cursor_modifier = modifier;
        editor.cursor_target_index = UINT32_MAX;
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_area, {});
        // A modifier transition refreshes even with a stationary pointer.
        editor.cursor_update_pending = true;
        if (modifier != nw::toolset::AreaTilePointerModifier::select
            && editor.selection.active()) {
            (void)update_area_tile_selection_preview(renderer, editor, active_area);
            return false;
        }
    }
    return editor.cursor_update_pending;
}

void clear_area_tile_cursor_target(ClientRenderer& renderer, AreaTileEditorState& editor,
    ObjectHandle active_area, AreaTilePointerModifier modifier)
{
    editor.cursor_target_index = UINT32_MAX;
    if (editor.stroke.active) {
        editor.stroke.has_last_target = false;
    } else if (modifier == nw::toolset::AreaTilePointerModifier::select
        || !editor.selection.active()) {
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_area, {});
    }
}

bool rotate_area_tile_group(ClientRenderer& renderer, AreaTileEditorState& editor,
    ObjectHandle active_area, std::optional<ClientViewportRect> viewport,
    AreaTilePointerModifier modifier, ShellController& shell)
{
    if (editor.stroke.active) {
        return false;
    }
    const bool has_cursor_point = editor.cursor_update_pending
        || editor.cursor_target_index != UINT32_MAX;
    const Rml::Vector2f cursor_point = editor.pending_cursor_point;
    if (!nw::toolset::rotate_area_tile_group_orientation(editor)) {
        shell.append_output("warn", editor.feedback);
        return true;
    }
    const std::string feedback = editor.feedback;
    if (viewport
        && has_cursor_point
        && viewport->contains_point(cursor_point.x, cursor_point.y)) {
        (void)update_area_tile_cursor(
            renderer, editor, active_area, cursor_point, *viewport, modifier, shell);
    } else {
        editor.preview_rows.clear();
        (void)renderer.update_viewer_area_tile_preview(
            active_area, {});
    }
    if (editor.feedback.empty()) {
        editor.feedback = feedback;
    }
    return true;
}

} // namespace nw::toolset
