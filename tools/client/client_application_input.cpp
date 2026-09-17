#include "client_application_input.hpp"
#include "client_application_commands.hpp"
#include "client_application_editor.hpp"
#include "client_application_keys.hpp"
#include "client_application_pointer.hpp"
#include "client_application_release.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "client_preferences.hpp"
#include "client_runtime.hpp"
#include "runtime_input.hpp"
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
nw::toolset::ClientInputOwnership client_input_ownership(const ClientApplicationState& state)
{
    using namespace nw::toolset;
    const bool preview = state.play_preview.session.active() || state.play_preview.placement_pending();
    const auto map = client_input_map(ClientControlRole::editor, preview);
    const bool ui_pointer_gesture = state.shell_view.bottom_dock_resizing || state.shell_view.left_dock_resizing
        || state.shell_view.output_selection.dragging || state.workspace_view.workspace_tab_dragging
        || state.managed_list_reorder.active() || state.project_blueprint_drag.active();
    return {
        .map = map,
        .pointer_owner = state.viewer_viewport_pointer_owner != ClientPointerOwner::none ? state.viewer_viewport_pointer_owner
            : ui_pointer_gesture                                                         ? ClientPointerOwner::toolset
                                                                                         : ClientPointerOwner::none,
        .world_available = preview && state.workspace.active_tab_id() == state.play_preview.tab_id
            && state.backend.module_generation() == state.play_preview.module_generation
            && nw::kernel::objects().valid(state.play_preview.area)
            && (state.play_preview.placement_pending() || nw::kernel::objects().valid(state.play_preview.session.actor())),
        .command_modal = state.backend.blueprint_operation_active()
            || state.backend.blueprint_publication_pending(),
        .world_input_blocked = state.loading.module_dialog_open || state.loading.project_load.active(),
    };
}

nw::toolset::ClientInputRoute client_world_pointer_route(const SDL_Event& event, SDL_Window* window,
    Rml::Context* context, Rml::Context* palette_context, Rml::ElementDocument* palette_doc,
    const ClientApplicationState& state, WorkspaceViewerViewportKind viewport)
{
    auto ownership = client_input_ownership(state);
    ownership.world_available = ownership.world_available && viewport == WorkspaceViewerViewportKind::area;
    return nw::toolset::resolve_client_event_input_route(event, window, context, palette_context, palette_doc,
        state.command_view.command_overlay_document, ownership);
}

nw::toolset::ClientUiActionOwner client_ui_action_owner(const ClientApplicationState& state)
{
    using namespace nw::toolset;
    const auto displayed = state.workbench.object_details.object;
    const auto script = state.smalls.active_object();
    const auto area = state.smalls.active_area();
    const bool running = state.play_preview.session.active();
    const bool placing = state.play_preview.placement_pending();
    return capture_client_ui_action_owner(state.workspace, {
                                                               .displayed_object = displayed,
                                                               .script_object = script,
                                                               .script_area = area,
                                                               .module_generation = state.backend.module_generation(),
                                                               .resource_generation = nw::kernel::resman().generation(),
                                                               .map = client_input_map(ClientControlRole::editor, running || placing),
                                                               .surface = state.workbench.object_workbench_surface,
                                                               .preview = running ? ClientPreviewPhase::running : placing ? ClientPreviewPhase::placing_actor
                                                                   : state.play_preview.selecting_actor                   ? ClientPreviewPhase::selecting_actor
                                                                                                                          : ClientPreviewPhase::inactive,
                                                               .area_surface = static_cast<uint8_t>(state.area_workspace_surface),
                                                               .live_objects = static_cast<uint8_t>((nw::kernel::objects().valid(displayed) ? 1 : 0) | (nw::kernel::objects().valid(script) ? 2 : 0) | (nw::kernel::objects().valid(area) ? 4 : 0)),
                                                               .command_modal = state.command_view.command_form.has_value() || state.command_view.command_palette_ui_visible || state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending(),
                                                               .world_blocked = state.loading.module_dialog_open || state.loading.project_load.active(),
                                                           });
}

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}

Rml::Element* find_ancestor_with_class(Rml::Element* element, std::string_view class_name)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->IsClassSet(class_name.data())) {
            return cursor;
        }
    }
    return nullptr;
}

Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == id) {
            return cursor;
        }
    }
    return nullptr;
}

bool point_within_viewport(ClientViewportRect rect, Rml::Vector2f point)
{
    return rect.contains_point(point.x, point.y);
}

bool shift_only(SDL_Keymod modifiers) noexcept
{
    return (modifiers & SDL_KMOD_SHIFT) != 0
        && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;
}

nw::toolset::AreaTilePointerModifier area_tile_pointer_modifier(
    SDL_Keymod modifiers) noexcept
{
    if ((modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0) {
        return nw::toolset::AreaTilePointerModifier::blocked;
    }
    return (modifiers & SDL_KMOD_SHIFT) != 0
        ? nw::toolset::AreaTilePointerModifier::select
        : nw::toolset::AreaTilePointerModifier::none;
}

nw::toolset::AreaTilePointerButton area_tile_pointer_button(
    uint8_t button) noexcept
{
    if (button == SDL_BUTTON_LEFT) {
        return nw::toolset::AreaTilePointerButton::primary;
    }
    if (button == SDL_BUTTON_RIGHT) {
        return nw::toolset::AreaTilePointerButton::secondary;
    }
    return nw::toolset::AreaTilePointerButton::other;
}

bool command_palette_contains_point(
    Rml::ElementDocument* palette_doc, const ClientApplicationState& state, Rml::Vector2f point)
{
    return state.shell.command_palette_visible
        && point_within_element(palette_doc, "command_palette", point);
}

void clear_rml_focus(Rml::Context* context)
{
    if (auto* focus = context ? context->GetFocusElement() : nullptr) {
        focus->Blur();
    }
}

void focus_workspace_viewport(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    clear_rml_focus(doc ? doc->GetContext() : nullptr);
    state.viewer_viewport_focused = true;
    if (state.command_view.command_palette_restore_captured) {
        state.command_view.command_palette_restore_focus_id.clear();
        state.command_view.command_palette_restore_viewport_focus = true;
    }
}

bool viewport_mouse_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const ClientApplicationState& state)
{
    if (state.shell.bottom_dock_visible() && point_within_element(doc, "bottom_dock", point)) {
        return true;
    }
    if (state.loading.module_dialog_open) {
        return true;
    }

    if (!top_hit) {
        return false;
    }

    return !find_ancestor_with_id(top_hit, "workspace_viewer_viewport");
}

bool recent_list_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const ClientApplicationState& state)
{
    if (!doc) {
        return true;
    }

    if (find_ancestor_with_id(top_hit, "recent_list") || find_ancestor_with_id(top_hit, "panel")) {
        return false;
    }

    if (state.shell.bottom_dock_visible() && point_within_element(doc, "bottom_dock", point)) {
        return true;
    }

    return false;
}

std::string get_input_value(Rml::ElementDocument* doc, const char* id)
{
    if (!doc) {
        return {};
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            return control->GetValue();
        }
        return input->GetAttribute<Rml::String>("value", "");
    }
    return {};
}

void set_input_value(Rml::ElementDocument* doc, const char* id, std::string_view value)
{
    if (!doc) {
        return;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            control->SetValue(Rml::String(value));
            return;
        }
        input->SetAttribute("value", Rml::String(value));
    }
}

void poll_client_input(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, ObjectWorkbenchChangeListener& object_workbench_change_listener, bool& running)
{
    auto* window = surfaces.window;
    auto* context = surfaces.context;
    auto* palette_context = surfaces.palette_context;
    auto* fps_context = surfaces.fps_context;
    auto* doc = surfaces.doc;
    auto* palette_doc = surfaces.palette_doc;
    auto& system_interface = *surfaces.system_interface;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        nw::toolset::ClientInputDispatchState dispatch;
        clear_inactive_object(state);
        if (state.project_blueprint_drag.active()
            && !project_blueprint_drag_context_matches(state)) {
            cancel_project_blueprint_drag(doc, state);
        }
        if (state.area_object_placement.active()
            && (state.workspace.active_tab_id() != state.area_object_placement.tab_id
                || (state.area_object_placement.object.type != nw::ObjectType::invalid
                    && renderer.area_viewer_object() != state.area_object_placement.area))) {
            cancel_area_object_placement(renderer, state);
        }
        if (state.area_object_drag.active
            && state.smalls.active_object() != state.area_object_drag.before.owner) {
            cancel_area_object_drag(renderer, state);
        }
        if (state.loading.open_module_dialog_event != 0 && event.type == state.loading.open_module_dialog_event) {
            handle_open_module_dialog_result(window, doc, state, event);
            continue;
        }
        if (consume_terminal_toggle_text_input(state, event)) {
            continue;
        }

        if (!nw::toolset::valid_client_pointer_event(event)) {
            cancel_area_tile_stroke(renderer, state);
            cancel_area_object_drag(renderer, state);
            cancel_area_object_placement(renderer, state);
            cancel_project_blueprint_drag(doc, state);
            nw::toolset::clear_managed_list_reorder(state.managed_list_reorder, doc);
            clear_workspace_tab_drag(state);
            (void)end_bottom_dock_resize(state);
            (void)end_left_dock_resize(state);
            state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
            state.shell_view.output_selection.dragging = false;
            state.browser.pressed_recent_index = -1;
            nw::toolset::discard_runtime_pointer_input(state.runtime_input);
            nw::toolset::cancel_client_pointer_interactions(context, palette_context, event);
            system_interface.SetMouseCursor("arrow");
            continue;
        }

        if ((state.backend.blueprint_operation_active() || state.backend.blueprint_publication_pending())) {
            if (event.type == SDL_EVENT_QUIT) {
                if (state.backend.blueprint_progress().stage == "recovery" && !state.backend.blueprint_worker_active()
                    && !state.workspace.has_dirty_tabs()) {
                    running = false;
                    continue;
                }
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Blueprint update in progress",
                    "Finish or cancel the blueprint operation before quitting.", window);
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP
                || event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING
                || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL) {
                (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                    nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                continue;
            }
        }
        if (state.command_view.command_form) {
            if (event.type == SDL_EVENT_KEY_DOWN) {
                const auto key = nw::toolset::handle_command_form_key(
                    state.command_view, state.backend, state.loading.project_load.active(),
                    palette_context, event.key);
                if (key.action_index) {
                    run_command_form_action(window, doc, state, *key.action_index);
                }
                if (key.handled) { continue; }
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP
                || event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING
                || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL) {
                (void)nw::toolset::forward_client_input(dispatch, nw::toolset::ClientRmlRecipient::command,
                    nw::toolset::ClientRmlForwardPhase::after_native, palette_context, window, event);
                continue;
            }
        }

        switch (event.type) {
        case SDL_EVENT_QUIT: {
            if (state.loading.project_import.active()) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Import in progress",
                    "Please wait for the module import to finish before quitting.", window);
                break;
            }
            if (!state.workspace.has_dirty_tabs()) {
                running = false;
                break;
            }
            const nw::toolset::CommandPrompt prompt{
                .id = "workspace.quit",
                .title = "Unsaved documents",
                .message = "Save changes before quitting?",
                .detail = "Discard closes all documents without saving their edits.",
                .actions = {
                    {"save", "Save All", "toolset.save_all", {}},
                    {"discard", "Discard", {}, {}},
                    {"cancel", "Cancel", {}, {}},
                },
            };
            const auto action = show_command_prompt(window, prompt);
            if (action && action->id == "discard") {
                running = false;
            } else if (action && action->id == "save") {
                const auto result = dispatch_command_flow(window, state,
                    "toolset.save_all", {}, nw::toolset::CommandSource::shortcut);
                refresh_workspace_view(doc, state);
                if (result.ok() && !state.workspace.has_dirty_tabs()) {
                    running = false;
                } else {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed", result.message.c_str(), window);
                }
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            if (process_client_key_down(event, dispatch, surfaces, renderer, state) == ClientEventFlow::next) { continue; }
            break;
        case SDL_EVENT_KEY_UP:
            if (state.play_preview.session.active()) {
                dispatch.native_handled = true;
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            nw::toolset::open_runtime_gamepad(
                state.runtime_input, event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (state.runtime_input.gamepad
                && SDL_GetGamepadID(state.runtime_input.gamepad.get())
                    == event.gdevice.which) {
                nw::toolset::close_runtime_gamepad(state.runtime_input);
                if (state.play_preview.session.active()) {
                    append_output(state, "warn",
                        "Play-preview controller disconnected");
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (state.play_preview.session.active()) {
                const auto route = nw::toolset::resolve_client_event_input_route(event, window, context,
                    palette_context, palette_doc, state.command_view.command_overlay_document, client_input_ownership(state));
                (void)nw::toolset::apply_runtime_pc_controller_button(state.runtime_input, event, route);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (process_client_pointer_down(event, dispatch, surfaces, renderer, state) == ClientEventFlow::next) { continue; }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (process_client_pointer_motion(event, dispatch, surfaces, renderer, state) == ClientEventFlow::next) { continue; }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (process_client_pointer_wheel(event, dispatch, surfaces, renderer, state) == ClientEventFlow::next) { continue; }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (process_client_pointer_up(event, dispatch, surfaces, renderer, state) == ClientEventFlow::next) { continue; }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (state.project_blueprint_drag.active()) {
                cancel_project_blueprint_drag(doc, state);
                state.browser.pressed_recent_index = -1;
                system_interface.SetMouseCursor("arrow");
            }
            if (state.area_object_placement.active()) {
                cancel_area_object_placement(renderer, state);
                state.browser.pressed_recent_index = -1;
                system_interface.SetMouseCursor("arrow");
            }
            state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
            state.shell_view.output_selection.dragging = false;
            hide_object_variable_warning_tooltip(doc, state);
            set_recent_hover(doc, state, -1);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            const auto pixels = query_window_pixels(window);
            surfaces.frame_width = pixels.first;
            surfaces.frame_height = pixels.second;
            state.workspace_view.workspace_tab_scroll_pending = true;
            state.workbench.object_workbench_tab_scroll_pending = true;
            log_window_metrics(window, "pixel-size-changed");
        } break;
        case SDL_EVENT_WINDOW_RESIZED: {
            const auto window_size = query_window_size(window);
            surfaces.width = window_size.first;
            surfaces.height = window_size.second;
            log_window_metrics(window, "resized");
            renderer.on_resize(static_cast<uint32_t>(surfaces.width), static_cast<uint32_t>(surfaces.height), context);
            fps_context->SetDimensions(Rml::Vector2i(surfaces.frame_width, surfaces.frame_height));
            palette_context->SetDimensions(Rml::Vector2i(surfaces.frame_width, surfaces.frame_height));
            apply_bottom_dock_height(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::bottom).size_px);
            apply_left_dock_width(doc, state, window, state.shell.docks.pane(nw::toolset::DockRegion::left).size_px);
            state.workspace_view.workspace_tab_scroll_pending = true;
            state.workbench.object_workbench_tab_scroll_pending = true;
            break;
        }
        default:
            break;
        }
        if (nw::toolset::client_input_forwarding_pending(dispatch)) {
            const auto route = nw::toolset::resolve_client_forward_route(event, window, palette_doc,
                nw::toolset::client_input_map(nw::toolset::ClientControlRole::editor,
                    state.play_preview.session.active() || state.play_preview.placement_pending()));
            const bool targets_palette = route.rml == nw::toolset::ClientRmlRecipient::command;
            (void)nw::toolset::forward_client_input(dispatch,
                route.rml, route.phase,
                targets_palette ? palette_context : context, window, event);
            const bool sound_volume_gesture_ended = !targets_palette
                && ((event.type == SDL_EVENT_MOUSE_BUTTON_UP
                        && event.button.button == SDL_BUTTON_LEFT)
                    || event.type == SDL_EVENT_KEY_DOWN);
            if (sound_volume_gesture_ended) {
                (void)object_workbench_change_listener.commit_sound_volume();
            }
            if (!targets_palette && state.shell.output_panel_visible()
                && output_scroll_input(doc, context, window, event)) {
                observe_output_scroll(doc, state);
            }
        }
    }
}

} // namespace nw::toolset::client_application_detail
