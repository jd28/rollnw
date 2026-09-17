#include "client_application_keys.hpp"
#include "client_application_commands.hpp"
#include "client_application_editor.hpp"
#include "client_application_input.hpp"
#include "client_application_preview.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "editor_input.hpp"
#include "runtime_input.hpp"
#include "smalls_rmlui.hpp"
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
ClientEventFlow process_client_key_down(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state)
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
        if (state.play_preview.session.active()
            || state.play_preview.placement_pending()) {
            if (!event.key.repeat
                && (event.key.key == SDLK_F9 || event.key.key == SDLK_ESCAPE)) {
                stop_play_preview(renderer, system_interface, doc, state);
            } else if (!event.key.repeat
                && event.key.key == SDLK_F8
                && state.play_preview.session.active()) {
                nw::toolset::toggle_play_preview_navigation_debug(renderer, state.play_preview, state.shell);
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (!event.key.repeat && (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_F9)
            && state.play_preview.selecting_actor) {
            restore_play_preview_picker_shell(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.area_workspace_surface
                == AreaWorkspaceSurface::tiles) {
            if (cancel_area_tile_action(renderer, state)) {
                sync_area_tile_palette_window(doc, state, true);
            } else {
                (void)set_area_workspace_surface(renderer, state,
                    AreaWorkspaceSurface::properties);
                refresh_workspace_content(doc, state);
            }
            system_interface.SetMouseCursor("arrow");
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (!event.key.repeat && event.key.key == SDLK_F9) {
            if (state.area_workspace_surface
                == AreaWorkspaceSurface::tiles) {
                (void)set_area_workspace_surface(renderer, state,
                    AreaWorkspaceSurface::properties);
                refresh_workspace_content(doc, state);
            }
            (void)prepare_play_preview(renderer, system_interface,
                doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
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
        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.area_object_placement.active()) {
            cancel_area_object_placement(renderer, state);
            state.browser.pressed_recent_index = -1;
            system_interface.SetMouseCursor("arrow");
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (state.area_object_placement.active()
            && state.area_object_placement.threshold_crossed) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const std::array shortcut_inputs{nw::toolset::EditorShortcutInput{
            .key = event.key.key, .modifiers = event.key.mod, .repeat = event.key.repeat}};
        std::array<nw::toolset::EditorShortcutAction, 1> shortcut_actions{};
        (void)nw::toolset::resolve_editor_shortcut_actions(shortcut_inputs, shortcut_actions);
        const auto shortcut = shortcut_actions.front();
        if (shortcut == nw::toolset::EditorShortcutAction::palette_toggle) {
            cancel_area_tile_stroke(renderer, state);
            append_command_result(state, dispatch_command(state, "rollnw.client.palette.toggle", {}, nw::toolset::CommandSource::shortcut));
            toggle_command_palette(context, palette_context, doc, palette_doc, state, state.shell.command_palette_visible);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (state.shell.command_palette_visible && event.key.key == SDLK_ESCAPE) {
            toggle_command_palette(context, palette_context, doc, palette_doc, state, false);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.workbench.object_details_combobox.is_active()) {
            close_object_details_combobox(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && close_active_smalls_selector(doc)) {
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const auto field_key_effect = nw::toolset::handle_object_workbench_field_key(event.key,
            context, doc, state.workbench, state.workspace, state.backend, state.shell,
            command_context(state, nw::toolset::CommandSource::widget));
        if (field_key_effect != nw::toolset::ObjectWorkbenchFieldKeyEffect::none) {
            if (field_key_effect == nw::toolset::ObjectWorkbenchFieldKeyEffect::content_changed) { refresh_workspace_content(doc, state); }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && nw::toolset::close_appearance_selector_for_escape(state.workbench.appearance_view,
                nw::toolset::object_workbench_target(state.workbench, state.workspace), state.backend.module_generation())) {
            refresh_workspace_content(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.managed_list_reorder.active()) {
            nw::toolset::clear_managed_list_reorder(
                state.managed_list_reorder, doc);
            system_interface.SetMouseCursor("arrow");
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.workbench.creature_view.creature_spell_filter_field != CreatureSpellFilterField::none) {
            clear_creature_spell_filter(state);
            refresh_workspace_content(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.area_object_drag.active) {
            cancel_area_object_drag(renderer, state);
            system_interface.SetMouseCursor("arrow");
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && state.workbench.object_workbench_surface == ObjectWorkbenchSurface::inventory
            && state.workbench.inventory_view.creature_inventory_selection >= 0) {
            state.workbench.inventory_view.creature_inventory_selection = -1;
            sync_creature_inventory_window(doc, state, true);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!event.key.repeat && event.key.key == SDLK_ESCAPE
            && renderer.clear_viewer_area_object_selection()) {
            state.smalls.clear_active_object();
            state.workbench.active_object_tab_id.clear();
            clear_active_object_details(state);
            state.smalls.refresh_ui_lists();
            refresh_workspace_content(doc, state);
            sync_object_details_window(doc, state, true);
            sync_creature_feat_window(doc, state, true);
            sync_creature_spell_window(doc, state, true);
            sync_creature_inventory_window(doc, state, true);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (state.shell.command_palette_visible
            && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
            && focused_element_has_id(palette_context, "command_input")) {
            nw::toolset::refresh_command_palette(palette_doc, state.command_view, state.backend);
            if (!state.command_view.commands.empty()) {
                execute_palette_command(window, context, palette_context, doc, palette_doc, state, state.command_view.commands.front().id);
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const auto catalog_key_effect = !state.shell.command_palette_visible
            ? nw::toolset::handle_appearance_selector_key(event.key, context, doc,
                  state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace),
                  state.backend_ready ? nw::kernel::resman().generation() : 0, state.backend, state.shell,
                  command_context(state, nw::toolset::CommandSource::widget))
            : nw::toolset::AppearanceSelectorKeyEffect::none;
        if (catalog_key_effect != nw::toolset::AppearanceSelectorKeyEffect::none) {
            if (catalog_key_effect == nw::toolset::AppearanceSelectorKeyEffect::content_changed) { refresh_workspace_content(doc, state); }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        auto* focused_managed_list = find_ancestor_with_class(
            context->GetFocusElement(), "managed_list_cycle");
        const bool managed_list_cycle_focused = !state.shell.command_palette_visible
            && !state.viewer_viewport_focused
            && focused_managed_list
            && !(event.key.mod
                & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI
                    | SDL_KMOD_SHIFT));
        if (managed_list_cycle_focused
            && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)
            && cycle_managed_list(doc, state, focused_managed_list,
                event.key.key == SDLK_UP ? -1 : 1)) {
            state.viewer_viewport_focused = false;
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        auto spell_key = nw::toolset::begin_creature_spell_filter_key(event.key,
            context, state.workbench.creature_view,
            nw::toolset::object_workbench_target(state.workbench, state.workspace),
            state.shell.command_palette_visible);
        if (spell_key.kind != nw::toolset::CreatureSpellFilterKeyKind::none) {
            if (spell_key.refresh_content) { refresh_workspace_content(doc, state); }
            nw::toolset::finish_creature_spell_filter_key(spell_key, doc,
                state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const auto output_key = nw::toolset::handle_shell_output_key(event.key, context, state.shell_view, state.shell);
        if (output_key.handled) {
            if (output_key.clipboard) { system_interface.SetClipboardText(*output_key.clipboard); }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (shortcut == nw::toolset::EditorShortcutAction::close_tab) {
            cancel_area_tile_stroke(renderer, state);
            if (ensure_backend_ready(state)) {
                dispatch_command_flow(
                    window, state, "workspace.close_tab", {}, nw::toolset::CommandSource::shortcut);
                refresh_workspace_view(doc, state);
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (shortcut == nw::toolset::EditorShortcutAction::save_tab || shortcut == nw::toolset::EditorShortcutAction::save_all) {
            cancel_area_tile_stroke(renderer, state);
            if (ensure_backend_ready(state)) {
                dispatch_command_flow(
                    window, state, shortcut == nw::toolset::EditorShortcutAction::save_all ? "toolset.save_all" : "workspace.save_tab",
                    {}, nw::toolset::CommandSource::shortcut);
                refresh_workspace_view(doc, state);
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }
        if (shortcut == nw::toolset::EditorShortcutAction::undo || shortcut == nw::toolset::EditorShortcutAction::redo) {
            cancel_area_tile_stroke(renderer, state);
            if (ensure_backend_ready(state)) {
                dispatch_command_flow(window,
                    state,
                    shortcut == nw::toolset::EditorShortcutAction::undo ? "command.undo" : "command.redo",
                    {},
                    nw::toolset::CommandSource::shortcut);
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (shortcut == nw::toolset::EditorShortcutAction::output_toggle) {
            append_command_result(state, dispatch_command(state, "rollnw.client.output.toggle", {}, nw::toolset::CommandSource::shortcut));
            toggle_output_panel(doc, state, state.shell.output_panel_visible());
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (shortcut == nw::toolset::EditorShortcutAction::terminal_toggle) {
            append_command_result(state, dispatch_command(state, "rollnw.client.terminal.toggle", {}, nw::toolset::CommandSource::shortcut));
            toggle_terminal(doc, state, state.shell.terminal_visible());
            state.shell_view.suppress_terminal_toggle_text_input = true;
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        const bool terminal_input_focused = focused_element_has_id(context, "terminal_input");
        if (state.shell.terminal_visible() && terminal_input_focused && event.key.key == SDLK_TAB) {
            complete_terminal_command(doc, state);
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (state.shell.terminal_visible() && terminal_input_focused && event.key.key == SDLK_RETURN) {
            const std::string line = get_input_value(doc, "terminal_input");
            if (!line.empty()) {
                append_terminal(state, "cmd", std::string("> ") + line);
                if (state.backend.is_open_module_dialog_invocation(line)) {
                    show_open_module_dialog(window, state);
                } else if (state.backend.is_open_project_dialog_invocation(line)) {
                    show_open_project_dialog(window, state);
                } else if (ensure_backend_ready(state)) {
                    const bool was_showing_areas = state.shell.showing_areas;
                    const bool was_showing_project = state.shell.showing_project_tree;
                    auto result = state.backend.console_execute(line, command_context(state, nw::toolset::CommandSource::terminal));
                    result = resolve_command_result(
                        window, state, std::move(result), nw::toolset::CommandSource::terminal, true);
                    if (result.ok() && state.shell.showing_project_tree) {
                        remember_recent_project(state, state.backend.current_project_dir());
                    }
                    sync_shell_visibility(context, palette_context, doc, palette_doc, state);
                    if (result.ok()
                        && (state.shell.showing_areas
                            || state.shell.showing_project_tree
                            || state.shell.showing_areas != was_showing_areas
                            || state.shell.showing_project_tree != was_showing_project)) {
                        refresh_recent_list(doc, state);
                    }
                    refresh_workspace_view(doc, state);
                } else {
                    append_terminal(state, "error", "Backend initialization failed");
                }
                set_input_value(doc, "terminal_input", "");
            }
            dispatch.native_handled = true;
            return ClientEventFlow::finish;
        }

        if (!nw::toolset::editor_key_has_binding(event.key.key)) { return ClientEventFlow::finish; }
        const auto key_viewport = active_workspace_viewer_viewport_request(doc, state, frame_width, frame_height);
        const auto key_facts = nw::toolset::capture_client_input_facts(event, window,
            context, palette_context, palette_doc, state.command_view.command_overlay_document, client_input_ownership(state));
        const std::array key_inputs{nw::toolset::EditorKeyInput{
            .facts = key_facts,
            .key = event.key.key,
            .modifiers = event.key.mod,
            .viewport = !key_viewport                                     ? nw::toolset::EditorViewportKind::none
                : key_viewport->kind == WorkspaceViewerViewportKind::area ? nw::toolset::EditorViewportKind::area
                                                                          : nw::toolset::EditorViewportKind::preview,
            .repeat = event.key.repeat,
            .viewport_focused = state.viewer_viewport_focused,
            .tiles_active = state.area_workspace_surface == AreaWorkspaceSurface::tiles,
            .tile_action_allowed = event.key.key == SDLK_R && area_tile_editor_action_allowed(state),
        }};
        std::array<nw::toolset::EditorKeyAction, 1> key_actions{};
        (void)nw::toolset::resolve_editor_key_actions(key_inputs, key_actions);
        const auto& key_action = key_actions.front();
        if (key_action.clear_viewport_focus) { state.viewer_viewport_focused = false; }
        switch (key_action.kind) {
        case nw::toolset::EditorKeyActionKind::rotate_tiles: {
            const std::optional<ClientViewportRect> area_viewport = key_viewport && key_viewport->kind == WorkspaceViewerViewportKind::area
                ? std::optional<ClientViewportRect>{key_viewport->rect}
                : std::nullopt;
            if (nw::toolset::rotate_area_tile_group(renderer, state.area_tile_editor,
                    active_workspace_area(state), area_viewport, area_tile_pointer_modifier(SDL_GetModState()), state.shell)) {
                sync_area_tile_palette_window(doc, state, true);
            }
            dispatch.native_handled = true;
            break;
        }
        case nw::toolset::EditorKeyActionKind::remove_object:
        case nw::toolset::EditorKeyActionKind::randomize_object_orientation:
            if (!synchronize_area_viewport_structure(renderer, state, true)) {
                dispatch.native_handled = true;
            } else {
                dispatch.native_handled = nw::toolset::handle_area_object_edit_key(renderer,
                    key_action.kind == nw::toolset::EditorKeyActionKind::remove_object
                        ? nw::toolset::AreaObjectEditKey::remove
                        : nw::toolset::AreaObjectEditKey::randomize_orientation,
                    state.backend, command_context(state, nw::toolset::CommandSource::shortcut), state.shell, state.smalls.active_object());
            }
            break;
        case nw::toolset::EditorKeyActionKind::camera:
            state.viewer_viewport_focused = true;
            cancel_area_object_drag(renderer, state);
            renderer.viewer_viewport_camera_command(key_action.camera, key_action.scale, key_viewport->rect);
            if (state.area_workspace_surface == AreaWorkspaceSurface::tiles && key_viewport->kind == WorkspaceViewerViewportKind::area) {
                state.area_tile_editor.cursor_update_pending = true;
            }
            dispatch.native_handled = true;
            break;
        case nw::toolset::EditorKeyActionKind::none:
            break;
        }
        return ClientEventFlow::finish;
    }

    return ClientEventFlow::finish;
}

} // namespace nw::toolset::client_application_detail
