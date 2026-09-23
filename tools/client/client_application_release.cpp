#include "client_application_release.hpp"
#include "client_application_commands.hpp"
#include "client_application_editor.hpp"
#include "client_application_input.hpp"
#include "client_application_preview.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "client_preferences.hpp"
#include "editor_input.hpp"
#include "rml_managed_list.hpp"
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
ClientEventFlow process_client_pointer_up(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    auto& system_interface = *surfaces.system_interface;
    const int frame_width = surfaces.frame_width;
    const int frame_height = surfaces.frame_height;

    if (state.area_tile_editor.stroke.active
        && event.button.button
            == state.area_tile_editor.stroke.pointer_button) {
        state.area_tile_editor.cursor_update_pending = false;
        const auto point = to_context_point(
            window, event.button.x, event.button.y);
        if (const auto viewport
            = active_workspace_viewer_viewport_request(
                doc, state, frame_width, frame_height);
            viewport
            && viewport->kind
                == WorkspaceViewerViewportKind::area
            && point_within_viewport(viewport->rect, point)) {
            (void)update_area_tile_cursor(
                renderer, state, point, *viewport);
        }
        commit_area_tile_stroke(renderer, state);
        system_interface.SetMouseCursor("arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (state.shell_view.output_selection.dragging
        && event.button.button == SDL_BUTTON_LEFT) {
        const auto point = to_context_point(
            window, event.button.x, event.button.y);
        if (const auto offset = output_text_offset_at_point(
                context, doc, state, point);
            offset && state.shell_view.output_selection.focus != *offset) {
            state.shell_view.output_selection.focus = *offset;
            state.shell.output_dirty = true;
        }
        state.shell_view.output_selection.dragging = false;
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (state.managed_list_reorder.active()
        && event.button.button == SDL_BUTTON_LEFT) {
        const auto point = to_context_point(
            window, event.button.x, event.button.y);
        const bool was_dragging = state.managed_list_reorder.dragging;
        (void)nw::toolset::update_managed_list_reorder(
            state.managed_list_reorder, doc,
            nw::toolset::ui_v1_host(), state.workbench.managed_lists,
            point.x, point.y, kWorkspaceTabDragThresholdPx,
            kManagedListAutoScrollEdgePx,
            kManagedListAutoScrollStepPx);
        const bool dragged = was_dragging
            || (state.managed_list_reorder.active()
                && state.managed_list_reorder.dragging);
        if (dragged) {
            if (nw::toolset::commit_managed_list_reorder(
                    state.managed_list_reorder, doc,
                    nw::toolset::ui_v1_host())) {
                dispatch_managed_list_events(state);
                refresh_smalls_elements(doc, state);
                (void)nw::toolset::sync_managed_lists(doc,
                    nw::toolset::ui_v1_host(),
                    state.workbench.managed_lists, true);
            }
        } else {
            nw::toolset::clear_managed_list_reorder(
                state.managed_list_reorder, doc);
        }
        system_interface.SetMouseCursor("arrow");
        if (dragged) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
    }
    if (state.project_blueprint_drag.active()
        && event.button.button == SDL_BUTTON_LEFT) {
        const bool blueprint_dragged = state.project_blueprint_drag.threshold_crossed;
        if (blueprint_dragged) {
            const auto point = to_context_point(
                window, event.button.x, event.button.y);
            (void)update_project_blueprint_drag(context, doc, state, point);
            commit_project_blueprint_drag(doc, state);
            state.browser.pressed_recent_index = -1;
            system_interface.SetMouseCursor("arrow");
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        cancel_project_blueprint_drag(doc, state);
    }
    if (state.project_blueprint_drag.active()
        && state.project_blueprint_drag.threshold_crossed) {
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (state.area_object_placement.active()
        && event.button.button == SDL_BUTTON_LEFT) {
        if (state.area_object_placement.region_drawing) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        const bool placement_dragged = state.area_object_placement.threshold_crossed;
        if (placement_dragged) {
            const auto point = to_context_point(window, event.button.x, event.button.y);
            const auto viewport = active_workspace_viewer_viewport_request(
                doc, state, frame_width, frame_height);
            update_area_object_placement(renderer, state, point, viewport);
            if (region_blueprint_resource(
                    state.area_object_placement.resource)) {
                auto& placement = state.area_object_placement;
                if (placement.phase
                        == AreaObjectPlacementPhase::ghost_valid
                    && placement.region_hover) {
                    placement.region_points.push_back(
                        *placement.region_hover);
                    placement.region_hover.reset();
                    placement.region_drawing = true;
                    placement.region_closing_valid = false;
                    placement.diagnostic.clear();
                    renderer.update_viewer_area_region_preview(
                        placement.region_points,
                        std::nullopt,
                        false);
                    state.browser.pressed_recent_index = -1;
                    system_interface.SetMouseCursor("cross");
                } else {
                    cancel_area_object_placement(renderer, state);
                    system_interface.SetMouseCursor("arrow");
                }
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
            commit_area_object_placement(renderer, state);
            state.browser.pressed_recent_index = -1;
            system_interface.SetMouseCursor("arrow");
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        state.area_object_placement = {};
    }
    if (state.area_object_placement.active()
        && state.area_object_placement.threshold_crossed) {
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (state.area_object_drag.active && event.button.button == SDL_BUTTON_LEFT) {
        const auto point = to_context_point(window, event.button.x, event.button.y);
        if (const auto viewport = active_workspace_viewer_viewport_request(
                doc, state, frame_width, frame_height)) {
            update_area_object_drag(renderer, state, point, *viewport);
            commit_area_object_drag(renderer, state);
        } else {
            cancel_area_object_drag(renderer, state);
        }
        system_interface.SetMouseCursor(
            point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if ((state.viewer_viewport_pointer_owner != nw::toolset::ClientPointerOwner::none)
        && (event.button.button == SDL_BUTTON_LEFT
            || event.button.button == SDL_BUTTON_MIDDLE
            || event.button.button == SDL_BUTTON_RIGHT)) {
        state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
        const auto point = to_context_point(window, event.button.x, event.button.y);
        system_interface.SetMouseCursor(point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
        dispatch.native_handled = true;
        return ClientEventFlow::finish;
    }
    if (event.button.button == SDL_BUTTON_LEFT) {
        if (end_bottom_dock_resize(state)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (end_left_dock_resize(state)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const auto point = to_context_point(window, event.button.x, event.button.y);
        const bool workspace_tab_was_dragging = state.workspace_view.workspace_tab_dragging;
        if (!state.workspace_view.workspace_tab_drag_id.empty()) {
            clear_workspace_tab_drag(state);
            if (workspace_tab_was_dragging) {
                refresh_workspace_view(doc, state);
                system_interface.SetMouseCursor(point_within_element(doc, "workspace_tabs", point) ? "pointer" : "arrow");
                dispatch.native_handled = true;
                return ClientEventFlow::finish;
            }
        }

        if (command_palette_contains_point(
                palette_doc, state, point)) {
            (void)nw::toolset::close_project_new_resource_menu(
                doc, state.shell_view);
            if (auto* hit = element_at_mouse(palette_context, window, event.button)) {
                if (auto* command_item = find_ancestor_with_class(hit, "command_item")) {
                    const std::string command_id = command_item->GetAttribute<Rml::String>("data-key", "");
                    execute_palette_command(window, context, palette_context, doc, palette_doc, state, command_id);
                } else {
                    (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                        nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                }
            }
            dispatch.native_handled = true;
            (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
            return ClientEventFlow::finish;
        }

        Rml::Element* recent_hit = nullptr;
        bool handled = false;
        const auto release_workspace_mouse_up = [&](nw::toolset::ClientRmlForwardPhase phase = nw::toolset::ClientRmlForwardPhase::before_native) {
            const auto before = client_ui_action_owner(state);
            if (dispatch.forwarded_recipient == nw::toolset::ClientRmlRecipient::none && context) {
                (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::toolset,
                    phase, context, window, event);
            }
            const bool unchanged = nw::toolset::same_client_ui_action_owner(before, client_ui_action_owner(state));
            if (!unchanged) {
                state.browser.pressed_recent_index = -1;
                dispatch.native_handled = true;
            }
            return unchanged;
        };
        auto* hit = element_at_mouse(context, window, event.button);
        if (!state.shell_view.new_resource_actions.empty()
            && !nw::toolset::project_new_resource_menu_contains(hit)) {
            (void)nw::toolset::close_project_new_resource_menu(
                doc, state.shell_view);
        }
        if (hit) {

            if (auto area_surface_click = nw::toolset::capture_area_workspace_surface_click(hit)) {
                if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                if (area_surface_click->surface) { (void)set_area_workspace_surface(renderer, state, *area_surface_click->surface); }
                refresh_workspace_content(doc, state);
                sync_area_tile_palette_window(doc, state, true);
                handled = true;
            } else if (auto light_click
                = nw::toolset::capture_area_tile_light_click(
                    hit, state.area_tile_editor)) {
                if (!release_workspace_mouse_up()) {
                    return ClientEventFlow::finish;
                }
                const auto effect = nw::toolset::apply_area_tile_light_click(
                    *light_click, state.area_tile_editor,
                    active_workspace_area(state),
                    area_tile_editor_action_allowed(state), state.backend,
                    command_context(
                        state, nw::toolset::CommandSource::widget),
                    state.shell);
                if (effect != nw::toolset::AreaTileLightClickEffect::none) {
                    sync_area_tile_palette_window(doc, state, true);
                }
                handled = true;
            } else if (auto tile_click = nw::toolset::capture_area_tile_palette_click(hit, state.area_tile_editor)) {
                if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                const auto effect = nw::toolset::apply_area_tile_palette_click(*tile_click, state.area_tile_editor, active_workspace_area(state));
                if (effect == nw::toolset::AreaTilePaletteClickEffect::folder_changed) {
                    cancel_area_tile_stroke(renderer, state);
                    clear_area_tile_selection(renderer, state);
                    nw::toolset::reset_area_tile_palette_folder_view(state.area_tile_editor);
                    refresh_workspace_content(doc, state);
                } else if (effect == nw::toolset::AreaTilePaletteClickEffect::selected) {
                    clear_area_tile_selection(renderer, state);
                    (void)renderer.update_viewer_area_tile_preview(active_workspace_area(state), {});
                }
                if (effect != nw::toolset::AreaTilePaletteClickEffect::none) { sync_area_tile_palette_window(doc, state, true); }
                handled = true;
            } else if (auto scroll_click = nw::toolset::capture_tab_scroll_click(hit,
                           kWorkspaceTabScrollStrip, "workspace_tab_scroll_button")) {
                if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                (void)nw::toolset::apply_tab_scroll_click(*scroll_click, doc,
                    kWorkspaceTabScrollStrip, state.workspace_view.workspace_tab_scroll_x);
                handled = true;
            } else if (auto workbench_scroll_click = nw::toolset::capture_tab_scroll_click(hit,
                           kObjectWorkbenchTabScrollStrip, "object_workbench_tab_scroll_button")) {
                if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                (void)nw::toolset::apply_tab_scroll_click(*workbench_scroll_click, doc,
                    kObjectWorkbenchTabScrollStrip, state.workbench.object_workbench_tab_scroll_x);
                handled = true;
            } else if (auto tab_click = nw::toolset::capture_workspace_tab_click(doc, hit, point, state.workspace)) {
                if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                const auto kind = tab_click->kind;
                if (kind != nw::toolset::WorkspaceTabClickKind::none && ensure_backend_ready(state)) {
                    if (auto invocation = nw::toolset::take_workspace_tab_click_command(*tab_click, state.workspace)) {
                        auto result = state.backend.execute_command(std::move(*invocation),
                            command_context(state, nw::toolset::CommandSource::widget));
                        if (kind == nw::toolset::WorkspaceTabClickKind::close) {
                            result = resolve_command_result(window, state, std::move(result), nw::toolset::CommandSource::widget);
                        } else {
                            append_command_result(state, result);
                        }
                        if (result.ok()) {
                            if (nw::toolset::sync_workspace_tab_click(doc, state.workspace_view,
                                    state.workspace, kind, tab_click->tab_id)) {
                                refresh_workspace_content(doc, state);
                            } else {
                                refresh_workspace_view(doc, state);
                            }
                            state.workspace_hover_refresh_pending = true;
                            state.workspace_hover_refresh_point = point;
                        }
                    }
                }
                handled = true;
            } else if (auto selected = nw::toolset::select_dialog_view_clicked_row(hit, state.dialog_view, state.workspace)) {
                const auto phase = *selected ? nw::toolset::ClientRmlForwardPhase::after_native
                                             : nw::toolset::ClientRmlForwardPhase::before_native;
                if (!release_workspace_mouse_up(phase)) { return ClientEventFlow::finish; }
                if (*selected) { nw::toolset::sync_dialog_view(doc, state.dialog_view, true); }
                handled = true;
            } else if (auto workbench_click = nw::toolset::capture_object_workbench_click(hit, point,
                           state.workbench, state.workspace, state.backend.module_generation(), nw::kernel::resman().generation())) {
                const auto phase = workbench_click->release_phase;
                if (phase == nw::toolset::ClientRmlForwardPhase::before_native
                    && !release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                nw::toolset::prepare_object_workbench_click(*workbench_click, doc);
                auto effect = nw::toolset::apply_object_workbench_click(*workbench_click, doc,
                    state.workbench, state.workspace, state.backend, state.shell,
                    command_context(state, nw::toolset::CommandSource::widget));
                if (phase == nw::toolset::ClientRmlForwardPhase::after_native
                    && !release_workspace_mouse_up(phase)) { return ClientEventFlow::finish; }
                if (effect.selection.kind == nw::toolset::PlacedAreaObjectClickKind::select) {
                    if (renderer.set_viewer_area_object_selection(effect.selection.object)) {
                        (void)renderer.focus_viewer_area_object_selection();
                    }
                } else if (effect.selection.kind == nw::toolset::PlacedAreaObjectClickKind::back) {
                    (void)renderer.clear_viewer_area_object_selection();
                }
                if (effect.sync_body && !sync_appearance_body_preview(renderer, state)) {
                    append_output(state, "error", "Failed to update the creature Appearance preview");
                }
                if (effect.refresh_content) { refresh_workspace_content(doc, state); }
                nw::toolset::finish_object_workbench_click(effect, *workbench_click, doc,
                    state.workbench, state.workspace, nw::kernel::resman().generation());
                handled = true;
            } else if (const auto activation = activate_managed_list(
                           doc, state, hit);
                activation.activated) {
                if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                if (activation.focus_target) {
                    (void)nw::toolset::focus_managed_list_target(
                        doc, *activation.focus_target);
                }
                handled = true;
            } else if (auto home_click = nw::toolset::capture_home_workspace_click(hit, state.browser, state.backend)) {
                if (home_click->kind != nw::toolset::HomeWorkspaceClickKind::none
                    && (home_click->kind != nw::toolset::HomeWorkspaceClickKind::select_area || ensure_backend_ready(state))) {
                    if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                    const auto kind = nw::toolset::consume_home_workspace_click(*home_click, state.browser, state.backend);
                    switch (kind) {
                    case nw::toolset::HomeWorkspaceClickKind::select_area: {
                        const auto result = dispatch_command_flow(window, state, "toolset.select_area",
                            {std::string_view{home_click->area_resref}}, nw::toolset::CommandSource::widget);
                        if (result.ok()) {
                            refresh_workspace_view(doc, state);
                            if (result.status == nw::toolset::CommandStatus::success) { focus_workspace_viewport(doc, state); }
                        }
                        break;
                    }
                    case nw::toolset::HomeWorkspaceClickKind::remove_project: {
                        const std::array indices{static_cast<size_t>(home_click->index)};
                        if (nw::toolset::forget_recent_project_preferences(state.shell_view.preferences_path,
                                state.shell.docks, state.browser.recent_projects, indices)
                            == nw::toolset::RecentProjectForgetStatus::save_failed) {
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to remove recent project",
                                "Could not save preferences. The project was kept in the recent list.", window);
                        }
                        refresh_workspace_content(doc, state);
                        break;
                    }
                    case nw::toolset::HomeWorkspaceClickKind::open_project: {
                        const auto& project = home_click->project;
                        if (!project.error.empty()) {
                            const auto message = project.error + ":\n" + project.path;
                            append_output(state, "error", message);
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to open project", message.c_str(), window);
                        } else if (ensure_backend_ready(state)) {
                            if (!queue_project_open(state, project.path, nw::toolset::CommandSource::widget)) {
                                append_output(state, "warn", "A project is already opening");
                            }
                        } else {
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to open project", "Backend initialization failed.", window);
                        }
                        refresh_workspace_view(doc, state);
                        break;
                    }
                    case nw::toolset::HomeWorkspaceClickKind::none:
                        break;
                    }
                }
                handled = true;
            } else if (auto shell_click = nw::toolset::capture_shell_ui_click(hit)) {
                const bool sync_visibility = shell_click->kind == nw::toolset::ShellUiClickKind::dock && !shell_click->value.empty();
                if (shell_click->kind
                    == nw::toolset::ShellUiClickKind::new_resource) {
                    if (!nw::toolset::close_project_new_resource_menu(
                            doc, state.shell_view)) {
                        auto result = state.backend.execute_command(
                            nw::toolset::CommandInvocation{"resource.new", {}},
                            command_context(state,
                                nw::toolset::CommandSource::widget));
                        if (!result.prompt
                            || !nw::toolset::open_project_new_resource_menu(
                                doc, state.shell_view, *result.prompt)) {
                            (void)resolve_command_result(window, state,
                                std::move(result),
                                nw::toolset::CommandSource::widget);
                        }
                    }
                } else if (shell_click->kind
                    == nw::toolset::ShellUiClickKind::new_resource_action) {
                    auto action = nw::toolset::take_project_new_resource_action(
                        *shell_click, state.shell_view);
                    (void)nw::toolset::close_project_new_resource_menu(
                        doc, state.shell_view);
                    if (action) {
                        std::vector<std::string_view> args;
                        args.reserve(action->args.size());
                        for (const auto& argument : action->args) {
                            args.push_back(argument);
                        }
                        (void)dispatch_command_flow(window, state,
                            action->command_id, std::move(args),
                            nw::toolset::CommandSource::widget);
                    }
                } else if (auto command
                    = nw::toolset::take_shell_ui_click_command(
                        *shell_click)) {
                    (void)resolve_command_result(window, state,
                        state.backend.execute_command(std::move(*command),
                            command_context(state, nw::toolset::CommandSource::widget)),
                        nw::toolset::CommandSource::widget);
                }
                if (sync_visibility) { sync_shell_visibility(context, palette_context, doc, palette_doc, state); }
                handled = true;
            } else {
                recent_hit = find_ancestor_with_class(hit, "recent_item");
            }
        }

        if (!handled) {
            auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
            if (!recent_hit && !recent_list_hit_blocked(doc, top_hit, point, state)) {
                recent_hit = recent_item_at_point(doc, point);
            }

            if (recent_hit) {
                auto click = nw::toolset::prepare_browser_row_click(doc, recent_hit,
                    state.browser, state.backend, state.shell, state.play_preview.selecting_actor);
                if (click.output_changed) { refresh_bottom_dock_view(doc, state); }
                if (click.kind != nw::toolset::BrowserRowClickKind::none
                    && (click.kind != nw::toolset::BrowserRowClickKind::open_resource || ensure_backend_ready(state))) {
                    if (!release_workspace_mouse_up()) { return ClientEventFlow::finish; }
                    const auto kind = nw::toolset::consume_browser_row_click(click,
                        state.browser, state.backend, state.shell, state.play_preview.selecting_actor);
                    if (kind == nw::toolset::BrowserRowClickKind::preview_actor) {
                        (void)prepare_play_preview(renderer, system_interface, doc, state, click.resource_path);
                    } else if (kind == nw::toolset::BrowserRowClickKind::open_resource
                        || kind == nw::toolset::BrowserRowClickKind::select_area) {
                        const auto argument = kind == nw::toolset::BrowserRowClickKind::open_resource
                            ? click.resource_path.generic_string()
                            : click.area_resref;
                        const auto result = dispatch_command_flow(window, state,
                            kind == nw::toolset::BrowserRowClickKind::open_resource
                                ? "toolset.open_resource"
                                : "toolset.select_area",
                            {std::string_view{argument}}, nw::toolset::CommandSource::widget);
                        if (result.ok()) {
                            refresh_workspace_view(doc, state);
                            const auto* tab = state.workspace.active_tab();
                            if (result.status == nw::toolset::CommandStatus::success
                                && (kind == nw::toolset::BrowserRowClickKind::select_area
                                    || (tab && tab->kind == nw::toolset::WorkspaceTabKind::area))) {
                                focus_workspace_viewport(doc, state);
                            }
                        }
                    }
                }
            }
        }
        state.browser.pressed_recent_index = -1;
        if (handled) {
            dispatch.native_handled = true;
            (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::toolset,
                nw::toolset::ClientRmlForwardPhase::after_native, context, window, event);
        }
    }
    return ClientEventFlow::finish;

    return ClientEventFlow::finish;
}

} // namespace nw::toolset::client_application_detail
