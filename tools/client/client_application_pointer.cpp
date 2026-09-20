#include "client_application_pointer.hpp"
#include "client_application_commands.hpp"
#include "client_application_editor.hpp"
#include "client_application_input.hpp"
#include "client_application_preview.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "editor_input.hpp"
#include "rml_managed_list.hpp"
#include "runtime_input.hpp"
#include "smalls_rmlui.hpp"
#include "virtual_combobox.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <utility>

namespace nw::toolset::client_application_detail {
ClientEventFlow process_client_pointer_down(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    auto& system_interface = *surfaces.system_interface;
    const int frame_width = surfaces.frame_width;
    const int frame_height = surfaces.frame_height;

    blur_focused_object_variable_input(context,
        to_context_point(window, event.button.x, event.button.y));
    if (event.button.button == SDL_BUTTON_RIGHT
        && state.area_tile_editor.stroke.active) {
        cancel_area_tile_stroke(renderer, state);
        system_interface.SetMouseCursor("arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (event.button.button == SDL_BUTTON_RIGHT
        && state.managed_list_reorder.active()) {
        nw::toolset::clear_managed_list_reorder(
            state.managed_list_reorder, doc);
        system_interface.SetMouseCursor("arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (event.button.button == SDL_BUTTON_RIGHT
        && state.project_blueprint_drag.active()) {
        cancel_project_blueprint_drag(doc, state);
        state.browser.pressed_recent_index = -1;
        system_interface.SetMouseCursor("arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (state.project_blueprint_drag.active()
        && state.project_blueprint_drag.threshold_crossed) {
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (event.button.button == SDL_BUTTON_RIGHT
        && state.area_object_placement.active()) {
        cancel_area_object_placement(renderer, state);
        state.browser.pressed_recent_index = -1;
        system_interface.SetMouseCursor("arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (event.button.button == SDL_BUTTON_LEFT
        && state.area_object_placement.region_drawing) {
        const auto point = to_context_point(
            window, event.button.x, event.button.y);
        const auto viewport = active_workspace_viewer_viewport_request(
            doc, state, frame_width, frame_height);
        if (viewport
            && viewport->kind == WorkspaceViewerViewportKind::area
            && point_within_viewport(viewport->rect, point)) {
            if (event.button.clicks >= 2) {
                complete_area_region_placement(
                    renderer, state, point, *viewport);
            } else {
                accept_area_region_point(
                    renderer, state, point, *viewport);
            }
        }
        system_interface.SetMouseCursor(
            state.area_object_placement.region_drawing
                ? "cross"
                : "arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (state.area_object_placement.active()
        && state.area_object_placement.threshold_crossed) {
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (event.button.button == SDL_BUTTON_LEFT
        || event.button.button == SDL_BUTTON_MIDDLE
        || event.button.button == SDL_BUTTON_RIGHT) {
        const auto point = to_context_point(window, event.button.x, event.button.y);
        if (command_palette_contains_point(
                palette_doc, state, point)) {
            (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
        if (event.button.button == SDL_BUTTON_LEFT
            && !nw::toolset::combobox_contains_element(top_hit)) {
            const bool closed_smalls
                = close_active_smalls_selector(doc);
            const bool closed_details
                = state.workbench.object_details_combobox.is_active();
            if (closed_details) {
                close_object_details_combobox(doc, state);
            }
            const bool closed_spell
                = state.workbench.creature_view.creature_spell_combobox.is_active();
            if (closed_spell) {
                clear_creature_spell_filter(state);
                refresh_workspace_content(doc, state);
            }
            if (closed_smalls || closed_details || closed_spell) {
                context->Update();
                top_hit = context->GetElementAtPoint(point);
            }
        }
        if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
            viewer_viewport
            && point_within_viewport(viewer_viewport->rect, point)
            && !viewport_mouse_hit_blocked(doc, top_hit, point, state)) {
            (void)close_active_smalls_selector(doc);
            if (state.workbench.appearance_view.appearance_selector_open) {
                close_appearance_selector(state);
                rebuild_active_appearances(
                    state, state.workbench.object_details.object);
                refresh_workspace_content(doc, state);
            }
            if (state.workbench.appearance_view.sound_resource_selector_open) {
                close_sound_resource_selector(state);
                refresh_workspace_content(doc, state);
            }
            state.viewer_viewport_focused = true;
            state.viewer_viewport_last_point = point;
            clear_rml_focus(context);
            if (viewer_viewport->kind
                    == WorkspaceViewerViewportKind::area
                && !synchronize_area_viewport_structure(
                    renderer, state, true)) {
                system_interface.SetMouseCursor("unavailable");
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            const auto world_route = client_world_pointer_route(event, window, context, palette_context,
                palette_doc, state, viewer_viewport->kind);
            if (world_route.native != nw::toolset::ClientNativeRecipient::editor
                && !(world_route.native == nw::toolset::ClientNativeRecipient::pc && world_route.sources.pointer)) {
                system_interface.SetMouseCursor("unavailable");
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            if (world_route.native == nw::toolset::ClientNativeRecipient::pc) {
                if (event.button.button == SDL_BUTTON_LEFT
                    && state.play_preview.placement_pending()) {
                    if (const auto ray
                        = nw::toolset::acquire_runtime_viewport_ray(renderer,
                            {point.x, point.y}, viewer_viewport->rect)) {
                        (void)start_play_preview_from_ray(renderer,
                            system_interface, doc, state, *ray);
                    } else {
                        state.play_preview.placement_diagnostic
                            = "Navigation ray could not be constructed";
                        system_interface.SetMouseCursor("cross");
                        append_output(state, "warn",
                            state.play_preview.placement_diagnostic);
                    }
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    nw::toolset::apply_play_preview_pointer_action(renderer, state.play_preview,
                        state.runtime_input, {point.x, point.y}, viewer_viewport->rect);
                } else if (event.button.button == SDL_BUTTON_RIGHT
                    || event.button.button == SDL_BUTTON_MIDDLE) {
                    state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::pc;
                    state.viewer_viewport_drag_mode
                        = ClientViewportDragMode::look;
                    system_interface.SetMouseCursor("grabbing");
                }
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            if (handle_area_tile_pointer_down(
                    renderer, system_interface, doc, state,
                    point, *viewer_viewport,
                    event.button.button)) {
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            const bool preview_orbit_drag = viewer_viewport->kind == WorkspaceViewerViewportKind::preview
                && event.button.button == SDL_BUTTON_LEFT;
            if (viewer_viewport->kind
                    == WorkspaceViewerViewportKind::area
                && event.button.button == SDL_BUTTON_LEFT
                && shift_only(SDL_GetModState())
                && add_encounter_spawn_point(
                    renderer, state, point, *viewer_viewport)) {
                system_interface.SetMouseCursor("arrow");
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            if (viewer_viewport->kind == WorkspaceViewerViewportKind::area
                && state.area_workspace_surface
                    == AreaWorkspaceSurface::objects
                && event.button.button == SDL_BUTTON_LEFT) {
                const bool control_pressed = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                renderer.select_viewer_area_object(
                    point.x,
                    point.y,
                    viewer_viewport->rect,
                    control_pressed
                        ? ClientAreaSelectionTarget::tile
                        : ClientAreaSelectionTarget::object);
                if (begin_area_object_drag(renderer, state, point, *viewer_viewport)) {
                    system_interface.SetMouseCursor("arrow");
                }
            }
            if (preview_orbit_drag || event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                cancel_area_object_drag(renderer, state);
                state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::editor;
                state.viewer_viewport_drag_mode = event.button.button == SDL_BUTTON_MIDDLE
                    ? ClientViewportDragMode::pan
                    : ClientViewportDragMode::look;
                system_interface.SetMouseCursor("grabbing");
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            state.viewer_viewport_focused = false;
        }
    }
    if (event.button.button == SDL_BUTTON_LEFT) {
        if (begin_bottom_dock_resize(context, window, doc, state, event.button)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (begin_left_dock_resize(context, window, doc, state, event.button)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        state.browser.pressed_recent_index = -1;
        const auto point = to_context_point(window, event.button.x, event.button.y);
        if (const auto offset = output_text_offset_at_point(
                context, doc, state, point)) {
            if (auto* output = find_el(doc, "output_list")) {
                output->Focus();
            }
            state.shell_view.output_selection.anchor = *offset;
            state.shell_view.output_selection.focus = *offset;
            state.shell_view.output_selection.dragging = true;
            state.shell.output_dirty = true;
            state.viewer_viewport_focused = false;
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (state.shell_view.output_selection.active()) {
            state.shell_view.output_selection.clear();
            state.shell.output_dirty = true;
        }
        auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
        if (nw::toolset::begin_workspace_tab_drag(doc, state.workspace_view, top_hit, point)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (!state.project_blueprint_drag.active()
            && nw::toolset::begin_managed_list_reorder(
                state.managed_list_reorder, top_hit,
                nw::toolset::ui_v1_host(), point.x, point.y)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (!recent_list_hit_blocked(doc, top_hit, point, state)) {
            if (auto* recent_item = recent_item_at_point(doc, point)) {
                const auto key = nw::toolset::client_row_key(recent_item);
                state.browser.pressed_recent_index = key.value_or(-1);
                if (key) {
                    const size_t index = static_cast<size_t>(
                        std::max(state.browser.pressed_recent_index, 0));
                    if (state.shell.showing_project_tree
                        && !state.play_preview.selecting_actor
                        && state.browser.pressed_recent_index >= 0
                        && index < state.browser.project_rows.size()) {
                        const auto& row = state.browser.project_rows[index].node;
                        if (!row.is_container()) {
                            const auto resource = nw::Resource::from_path(
                                row.relative_path, false);
                            const bool armed = arm_project_blueprint_drag(
                                state, resource, row.path, point);
                            if (!armed) {
                                arm_area_object_placement(
                                    renderer, state, resource, point);
                            }
                        }
                    }
                }
            }
        }
    }
    return ClientEventFlow::finish;

    return ClientEventFlow::finish;
}

ClientEventFlow process_client_pointer_motion(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    auto& system_interface = *surfaces.system_interface;
    const int frame_width = surfaces.frame_width;
    const int frame_height = surfaces.frame_height;
    {
        if (update_bottom_dock_resize(doc, state, window, event.motion)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (update_left_dock_resize(doc, state, window, event.motion)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const auto point = to_context_point(window, event.motion.x, event.motion.y);
        auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
        sync_object_variable_warning_tooltip(doc, state, top_hit, point,
            frame_width, frame_height);
        if (state.shell_view.output_selection.dragging) {
            if (const auto offset = output_text_offset_at_point(
                    context, doc, state, point);
                offset && state.shell_view.output_selection.focus != *offset) {
                state.shell_view.output_selection.focus = *offset;
                state.shell.output_dirty = true;
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (command_palette_contains_point(
                palette_doc, state, point)) {
            (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (nw::toolset::update_managed_list_reorder(
                state.managed_list_reorder, doc,
                nw::toolset::ui_v1_host(), state.workbench.managed_lists,
                point.x, point.y, kWorkspaceTabDragThresholdPx,
                kManagedListAutoScrollEdgePx,
                kManagedListAutoScrollStepPx)) {
            const auto& gesture = state.managed_list_reorder;
            const char* cursor = "arrow";
            if (gesture.dragging) {
                cursor = nw::toolset::managed_list_reorder_destination(
                             gesture)
                    ? "grabbing"
                    : "unavailable";
            }
            system_interface.SetMouseCursor(cursor);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (state.project_blueprint_drag.active()) {
            if (update_project_blueprint_drag(context, doc, state, point)) {
                const char* cursor = state.project_blueprint_drag.phase
                        == ProjectBlueprintDragPhase::target_valid
                    ? "cross"
                    : "unavailable";
                system_interface.SetMouseCursor(cursor);
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
        }
        if (state.area_object_placement.active()) {
            const auto viewport = active_workspace_viewer_viewport_request(
                doc, state, frame_width, frame_height);
            if (update_area_object_placement(renderer, state, point, viewport)) {
                const char* cursor = "unavailable";
                if (!state.area_object_placement.active()) {
                    cursor = "arrow";
                } else if (state.area_object_placement.phase == AreaObjectPlacementPhase::ghost_valid) {
                    cursor = "cross";
                }
                system_interface.SetMouseCursor(cursor);
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
        }
        if (state.viewer_viewport_pointer_owner != nw::toolset::ClientPointerOwner::none) {
            const float dx = point.x - state.viewer_viewport_last_point.x;
            const float dy = point.y - state.viewer_viewport_last_point.y;
            state.viewer_viewport_last_point = point;
            if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height)) {
                const auto world_route = client_world_pointer_route(event, window, context, palette_context,
                    palette_doc, state, viewer_viewport->kind);
                if (world_route.native == nw::toolset::ClientNativeRecipient::pc && world_route.sources.pointer) {
                    if (state.play_preview.session.active()) {
                        state.runtime_input.mouse_look_pixels.x += dx;
                        state.runtime_input.mouse_look_pixels.y += dy;
                    }
                } else if (world_route.native == nw::toolset::ClientNativeRecipient::editor) {
                    renderer.drag_viewer_viewport(
                        state.viewer_viewport_drag_mode, dx, dy,
                        viewer_viewport->rect);
                    if (state.area_workspace_surface
                        == AreaWorkspaceSurface::tiles) {
                        state.area_tile_editor.pending_cursor_point = point;
                        state.area_tile_editor.cursor_update_pending = true;
                    }
                } else {
                    state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
                    nw::toolset::discard_runtime_pointer_input(state.runtime_input);
                }
                system_interface.SetMouseCursor(state.viewer_viewport_pointer_owner != nw::toolset::ClientPointerOwner::none ? "grabbing" : "arrow");
            } else {
                state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
                nw::toolset::discard_runtime_pointer_input(state.runtime_input);
                system_interface.SetMouseCursor("arrow");
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (state.area_object_drag.active) {
            if (auto viewer_viewport = active_workspace_viewer_viewport_request(
                    doc, state, frame_width, frame_height)) {
                update_area_object_drag(renderer, state, point, *viewer_viewport);
                system_interface.SetMouseCursor(!state.area_object_drag.pointer.dragging ? "arrow"
                        : state.area_object_drag.valid                                   ? "grabbing"
                                                                                         : "unavailable");
            } else {
                cancel_area_object_drag(renderer, state);
                system_interface.SetMouseCursor("arrow");
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (state.area_workspace_surface
            == AreaWorkspaceSurface::tiles) {
            const auto viewer_viewport
                = active_workspace_viewer_viewport_request(
                    doc, state, frame_width, frame_height);
            if (viewer_viewport
                && viewer_viewport->kind
                    == WorkspaceViewerViewportKind::area
                && point_within_viewport(
                    viewer_viewport->rect, point)
                && !viewport_mouse_hit_blocked(
                    doc, top_hit, point, state)) {
                state.area_tile_editor.pending_cursor_point = point;
                const bool tile_shift
                    = area_tile_pointer_modifier(SDL_GetModState())
                    == nw::toolset::AreaTilePointerModifier::select;
                state.area_tile_editor.cursor_update_pending
                    = true;
                system_interface.SetMouseCursor(
                    tile_shift ? "pointer" : "cross");
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            state.area_tile_editor.cursor_update_pending = false;
            if (state.area_tile_editor.stroke.active) {
                state.area_tile_editor.stroke.has_last_target = false;
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            if (state.area_tile_editor.cursor_target_index != UINT32_MAX
                || !state.area_tile_editor.preview_rows.empty()) {
                state.area_tile_editor.cursor_target_index = UINT32_MAX;
                state.area_tile_editor.preview_rows.clear();
                (void)renderer.update_viewer_area_tile_preview(
                    active_workspace_area(state), {});
            }
        }

        if (state.play_preview.session.active()
            || state.play_preview.placement_pending()) {
            const auto viewer_viewport
                = active_workspace_viewer_viewport_request(
                    doc, state, frame_width, frame_height);
            if (viewer_viewport
                && viewer_viewport->kind
                    == WorkspaceViewerViewportKind::area
                && point_within_viewport(viewer_viewport->rect, point)
                && !viewport_mouse_hit_blocked(
                    doc, top_hit, point, state)) {
                system_interface.SetMouseCursor(nw::toolset::play_preview_pointer_cursor(
                    renderer, state.play_preview, {point.x, point.y}, viewer_viewport->rect));
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
        }

        auto tab_drag = nw::toolset::update_workspace_tab_drag(doc, state.workspace_view, state.workspace, point);
        if (tab_drag.handled) {
            system_interface.SetMouseCursor("grabbing");
            if (tab_drag.command) {
                const auto result = state.backend.execute_command(std::move(*tab_drag.command),
                    command_context(state, nw::toolset::CommandSource::widget));
                if (result.ok()) { refresh_workspace_view(doc, state); }
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        auto* list = find_el(doc, "recent_list");
        auto* search = find_el(doc, "recent_search");
        int hovered = -1;
        if (!recent_list_hit_blocked(doc, top_hit, point, state)
            && list && list->IsVisible(true)
            && (!search || !search->IsVisible(true)
                || !search->IsPointWithinElement(point))
            && list->IsPointWithinElement(point)) {
            if (auto* recent_item = find_recent_item_at(list, point)) {
                hovered = nw::toolset::client_row_key(recent_item).value_or(-1);
            }
        }
        set_recent_hover(doc, state, hovered);
        return ClientEventFlow::finish;
    }

    return ClientEventFlow::finish;
}

ClientEventFlow process_client_pointer_wheel(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    const int frame_width = surfaces.frame_width;
    const int frame_height = surfaces.frame_height;
    {
        if (state.project_blueprint_drag.active()
            && state.project_blueprint_drag.threshold_crossed) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (state.area_object_placement.active()
            && state.area_object_placement.threshold_crossed) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        const auto point = to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y);
        if (command_palette_contains_point(
                palette_doc, state, point)) {
            (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        auto* wheel_hit = context
            ? context->GetElementAtPoint(point)
            : nullptr;
        if (nw::toolset::combobox_popup_contains_element(
                wheel_hit)) {
            // RmlUi scrolls the open popup. Focused-field cycling is
            // only the closed combobox path.
            return ClientEventFlow::finish;
        }
        if (event.wheel.y != 0.0f) {
            const SDL_Keymod modifiers = SDL_GetModState();
            auto* focused_managed_list = find_ancestor_with_class(
                context->GetFocusElement(), "managed_list_cycle");
            const bool managed_list_cycle_focused
                = !state.shell.command_palette_visible
                && !state.loading.module_dialog_open
                && !state.viewer_viewport_focused
                && focused_managed_list
                && !(modifiers
                    & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                        | SDL_KMOD_SHIFT));
            if (managed_list_cycle_focused
                && cycle_managed_list(doc, state,
                    focused_managed_list,
                    event.wheel.y > 0.0f ? -1 : 1)) {
                state.viewer_viewport_focused = false;
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            if (auto viewer_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
                viewer_viewport && point_within_viewport(viewer_viewport->rect, point)) {
                const auto world_route = client_world_pointer_route(event, window, context, palette_context,
                    palette_doc, state, viewer_viewport->kind);
                if (world_route.native == nw::toolset::ClientNativeRecipient::pc && world_route.sources.pointer) {
                    if (state.play_preview.session.active()) { state.runtime_input.wheel_zoom += event.wheel.y; }
                    dispatch.native_handled = true;
                    return ClientEventFlow::finish;
                }
                if (world_route.native != nw::toolset::ClientNativeRecipient::editor) {
                    dispatch.native_handled = true;
                    return ClientEventFlow::finish;
                }
                const auto object = renderer.active_viewer_object();
                const std::array wheel_inputs{nw::toolset::EditorWheelInput{
                    .recipient = world_route.native,
                    .viewport = viewer_viewport->kind == WorkspaceViewerViewportKind::area ? nw::toolset::EditorViewportKind::area : nw::toolset::EditorViewportKind::preview,
                    .object_type = object.type,
                    .modifiers = modifiers,
                    .amount = event.wheel.y,
                    .text_focused = focused_text_input(context)}};
                std::array<nw::toolset::EditorWheelAction, 1> wheel_actions{};
                (void)nw::toolset::resolve_editor_wheel_actions(wheel_inputs, wheel_actions);
                cancel_area_object_drag(renderer, state);
                const auto& action = wheel_actions[0];
                if (action.kind == nw::toolset::EditorWheelActionKind::camera_zoom) {
                    renderer.zoom_viewer_viewport(action.amount, viewer_viewport->rect);
                    if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
                        state.area_tile_editor.pending_cursor_point = point;
                        state.area_tile_editor.cursor_update_pending = true;
                    }
                } else {
                    if (auto result = nw::toolset::apply_area_object_wheel_action(action, state.backend,
                            command_context(state, nw::toolset::CommandSource::renderer), object)) {
                        if (action.kind == nw::toolset::EditorWheelActionKind::sound_radius) {
                            append_command_result(state, *result);
                        } else {
                            nw::toolset::sync_area_object_after_command(renderer, state.shell, *result, state.smalls.active_object());
                        }
                    }
                }
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
        }
        if (point_within_element(doc, "workspace_tabs", point)) {
            const float delta = event.wheel.x != 0.0f ? event.wheel.x : -event.wheel.y;
            if (auto* tabs = find_el(doc, "workspace_tabs")) {
                state.workspace_view.workspace_tab_scroll_x = tabs->GetScrollLeft();
            }
            state.workspace_view.workspace_tab_scroll_x += delta * kTabScrollStepPx;
            apply_workspace_tab_scroll(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (point_within_element(doc, "object_workbench_tabs", point)) {
            const float delta = event.wheel.x != 0.0f ? event.wheel.x : -event.wheel.y;
            if (auto* tabs = find_el(doc, "object_workbench_tabs")) {
                state.workbench.object_workbench_tab_scroll_x = tabs->GetScrollLeft();
            }
            state.workbench.object_workbench_tab_scroll_x += delta * kTabScrollStepPx;
            apply_object_workbench_tab_scroll(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        return ClientEventFlow::finish;
    }

    return ClientEventFlow::finish;
}

} // namespace nw::toolset::client_application_detail
