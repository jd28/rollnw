#include "client_application_editor.hpp"
#include "area_tile_interaction.hpp"
#include "client_application_commands.hpp"
#include "client_application_input.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "client_application_workspace.hpp"
#include "editor_input.hpp"
#include "viewport_pointer_drag.hpp"
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
bool sync_area_tile_palette_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    const auto* tab = state.workspace.active_tab();
    const nw::ObjectHandle area = tab && tab->kind == nw::toolset::WorkspaceTabKind::area
        ? tab->document.object()
        : nw::ObjectHandle{};
    return nw::toolset::sync_area_tile_palette_window(doc, state.area_tile_editor, area,
        state.area_workspace_surface == AreaWorkspaceSurface::tiles,
        area_tile_pointer_modifier(SDL_GetModState()), force);
}

bool begin_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area
        || !nw::toolset::begin_area_object_drag(renderer, state.area_object_drag, point, viewport.rect)) { return false; }
    state.smalls.publish_active_object(state.area_object_drag.before.owner);
    state.workbench.active_object_tab_id = state.workspace.active_tab_id();
    return true;
}

bool update_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area) {
        nw::toolset::cancel_area_object_drag(renderer, state.area_object_drag);
        return false;
    }
    return nw::toolset::update_area_object_drag(renderer, state.area_object_drag, point, viewport.rect);
}

void cancel_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state)
{
    nw::toolset::cancel_area_object_drag(renderer, state.area_object_drag);
}

void commit_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state)
{
    if (!state.area_object_drag.active) { return; }
    nw::toolset::commit_area_object_drag(renderer, state.area_object_drag,
        state.smalls.active_object(), state.backend,
        command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

bool add_encounter_spawn_point(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    return nw::toolset::add_encounter_spawn_point(renderer, point, viewport.rect,
        state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

nw::ObjectHandle active_workspace_area(const ClientApplicationState& state) noexcept
{
    const auto* tab = state.workspace.active_tab();
    return tab && tab->kind == nw::toolset::WorkspaceTabKind::area
        ? tab->document.object()
        : nw::ObjectHandle{};
}

bool area_tile_editor_action_allowed(const ClientApplicationState& state) noexcept
{
    return state.area_workspace_surface == AreaWorkspaceSurface::tiles
        && active_workspace_area(state).type == nw::ObjectType::area
        && !state.command_view.command_form && !state.loading.module_dialog_open
        && !state.backend.blueprint_operation_active()
        && !state.backend.blueprint_publication_pending()
        && !state.shell.command_palette_visible
        && !state.play_preview.session.active();
}

std::optional<nw::toolset::AreaTileBrush> selected_area_tile_brush(
    const ClientApplicationState& state, uint8_t pointer_button) noexcept
{
    return nw::toolset::selected_area_tile_brush(state.area_tile_editor, area_tile_pointer_button(pointer_button));
}

bool area_tile_stroke_context_valid(const ClientApplicationState& state) noexcept
{
    return area_tile_editor_action_allowed(state)
        && state.area_tile_editor.stroke.active
        && active_workspace_area(state) == state.area_tile_editor.stroke.area;
}

bool synchronize_area_viewport_structure(
    ClientRenderer& renderer, ClientApplicationState& state, bool report_failure)
{
    const nw::ObjectHandle area = active_workspace_area(state);
    if (area.type != nw::ObjectType::area
        || state.stale_area_viewport != area) {
        return true;
    }
    cancel_area_object_placement(renderer, state);
    cancel_area_object_drag(renderer, state);
    cancel_area_tile_stroke(renderer, state);
    if (!renderer.rebuild_live_viewer_area(
            area, renderer.active_viewer_object())) {
        if (report_failure) {
            append_output(state, "error",
                "The area viewport is stale because its structural rebuild failed");
        }
        return false;
    }
    state.observed_area_structure_epoch
        = nw::toolset::object_mutation_state().area_structure_epoch;
    state.stale_area_viewport = nw::ObjectHandle{};
    return true;
}

void clear_area_tile_selection(ClientRenderer& renderer, ClientApplicationState& state)
{
    nw::toolset::clear_area_tile_selection(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool select_area_tiles(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area) { return false; }
    return nw::toolset::select_area_tiles(renderer, state.area_tile_editor,
        active_workspace_area(state), point, viewport.rect,
        area_tile_editor_action_allowed(state), state.shell);
}

bool cycle_area_tile_at_point(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (viewport.kind != WorkspaceViewerViewportKind::area) { return false; }
    return nw::toolset::cycle_area_tile_at_point(renderer, state.area_tile_editor,
        active_workspace_area(state), point, viewport.rect,
        area_tile_editor_action_allowed(state), state.backend,
        command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

bool handle_area_tile_pointer_down(
    ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    ClientApplicationState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport,
    uint8_t pointer_button)
{
    if (state.area_workspace_surface != AreaWorkspaceSurface::tiles
        || viewport.kind != WorkspaceViewerViewportKind::area) {
        return false;
    }

    const auto input = nw::toolset::AreaTilePointerInput{
        .button = area_tile_pointer_button(pointer_button),
        .modifier = area_tile_pointer_modifier(SDL_GetModState()),
        .secondary_paint_available
        = selected_area_tile_brush(state, SDL_BUTTON_RIGHT).has_value(),
    };
    const auto result
        = nw::toolset::resolve_area_tile_pointer_input(input);
    switch (result.action) {
    case nw::toolset::AreaTilePointerAction::select:
        (void)select_area_tiles(renderer, state, point, viewport);
        sync_area_tile_palette_window(doc, state, true);
        system_interface.SetMouseCursor("arrow");
        break;
    case nw::toolset::AreaTilePointerAction::cycle_variation:
        (void)cycle_area_tile_at_point(renderer, state, point, viewport);
        sync_area_tile_palette_window(doc, state, true);
        system_interface.SetMouseCursor("arrow");
        break;
    case nw::toolset::AreaTilePointerAction::paint: {
        clear_area_tile_selection(renderer, state);
        const bool began = begin_area_tile_stroke(
            renderer, state, point, viewport, pointer_button);
        system_interface.SetMouseCursor(
            began ? "cross" : "unavailable");
        break;
    }
    case nw::toolset::AreaTilePointerAction::none:
        break;
    }
    return result.consumed;
}

void cancel_area_tile_stroke(ClientRenderer& renderer, ClientApplicationState& state)
{
    nw::toolset::cancel_area_tile_stroke(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool cancel_area_tile_action(ClientRenderer& renderer, ClientApplicationState& state)
{
    return nw::toolset::cancel_area_tile_action(renderer, state.area_tile_editor, active_workspace_area(state));
}

bool open_area_tile_editor(ClientRenderer& renderer, ClientApplicationState& state)
{
    const nw::ObjectHandle area_handle = active_workspace_area(state);
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!area || area->tiles.empty()) {
        append_output(state, "warn", "The displayed area has no editable tiles");
        return false;
    }

    cancel_area_object_placement(renderer, state);
    cancel_area_object_drag(renderer, state);
    (void)renderer.clear_viewer_area_object_selection();
    auto& editor = state.area_tile_editor;
    state.area_workspace_surface = AreaWorkspaceSurface::tiles;
    if (!nw::toolset::reset_area_tile_editor(editor, area_handle)) {
        append_output(state, "error", editor.palette.diagnostic);
    }
    return true;
}

void close_area_tile_editor(ClientRenderer& renderer, ClientApplicationState& state)
{
    cancel_area_tile_stroke(renderer, state);
    state.area_tile_editor = {};
    (void)renderer.clear_viewer_area_object_selection();
}

bool set_area_workspace_surface(ClientRenderer& renderer,
    ClientApplicationState& state,
    AreaWorkspaceSurface surface)
{
    if (state.area_workspace_surface == surface) {
        return true;
    }
    if (surface == AreaWorkspaceSurface::tiles) {
        return open_area_tile_editor(renderer, state);
    }

    if (state.area_workspace_surface == AreaWorkspaceSurface::tiles) {
        close_area_tile_editor(renderer, state);
    } else {
        cancel_area_object_placement(renderer, state);
        cancel_area_object_drag(renderer, state);
        (void)renderer.clear_viewer_area_object_selection();
    }
    state.area_workspace_surface = surface;
    state.smalls.clear_active_object();
    state.workbench.active_object_tab_id.clear();
    clear_active_object_details(state);
    return true;
}

bool update_area_tile_cursor(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    if (state.area_workspace_surface != AreaWorkspaceSurface::tiles
        || viewport.kind != WorkspaceViewerViewportKind::area) { return false; }
    return nw::toolset::update_area_tile_cursor(renderer, state.area_tile_editor,
        active_workspace_area(state), point, viewport.rect,
        area_tile_pointer_modifier(SDL_GetModState()), state.shell);
}

void flush_area_tile_cursor_update(ClientRenderer& renderer,
    SDL_Window* window,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    ClientApplicationState& state,
    int frame_width,
    int frame_height)
{
    auto& editor = state.area_tile_editor;
    if (!area_tile_editor_action_allowed(state)) {
        return;
    }
    const auto modifier = area_tile_pointer_modifier(SDL_GetModState());
    if (!nw::toolset::prepare_area_tile_cursor_update(
            renderer, editor, active_workspace_area(state), modifier)) { return; }
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    (void)SDL_GetMouseState(&mouse_x, &mouse_y);
    const Rml::Vector2f point = to_context_point(window, mouse_x, mouse_y);
    editor.pending_cursor_point = point;
    editor.cursor_update_pending = false;
    const auto viewport = active_workspace_viewer_viewport_request(
        doc, state, frame_width, frame_height);
    auto* top_hit = context ? context->GetElementAtPoint(point) : nullptr;
    if (modifier != nw::toolset::AreaTilePointerModifier::blocked
        && SDL_GetMouseFocus() == window
        && viewport
        && viewport->kind == WorkspaceViewerViewportKind::area
        && point_within_viewport(viewport->rect, point)
        && !viewport_mouse_hit_blocked(doc, top_hit, point, state)) {
        (void)update_area_tile_cursor(renderer, state, point, *viewport);
        return;
    }
    nw::toolset::clear_area_tile_cursor_target(renderer, editor, active_workspace_area(state), modifier);
}

bool begin_area_tile_stroke(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport, uint8_t pointer_button)
{
    auto& editor = state.area_tile_editor;
    editor.cursor_update_pending = false;
    const auto brush = nw::toolset::selected_area_tile_brush(editor, area_tile_pointer_button(pointer_button));
    if (!brush) {
        editor.feedback = "Choose a terrain action";
        return false;
    }
    if (area_tile_editor_action_allowed(state)
        && !synchronize_area_viewport_structure(renderer, state, true)) { return true; }
    return nw::toolset::begin_area_tile_stroke(renderer, editor,
        active_workspace_area(state), point, viewport.rect, pointer_button, *brush,
        area_tile_pointer_modifier(SDL_GetModState()), area_tile_editor_action_allowed(state), state.shell);
}

void commit_area_tile_stroke(ClientRenderer& renderer, ClientApplicationState& state)
{
    if (!state.area_tile_editor.stroke.active) { return; }
    nw::toolset::commit_area_tile_stroke(renderer, state.area_tile_editor,
        active_workspace_area(state), area_tile_editor_action_allowed(state),
        state.stale_area_viewport == state.area_tile_editor.stroke.area, state.backend,
        command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

void arm_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state, nw::Resource resource, Rml::Vector2f point)
{
    nw::toolset::arm_area_object_placement(renderer, state.area_object_placement,
        std::move(resource), point, state.workspace.active_tab_id());
}

void cancel_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state)
{
    nw::toolset::cancel_area_object_placement(renderer, state.area_object_placement, state.shell);
}

bool update_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const std::optional<WorkspaceViewerViewportRequest>& viewport)
{
    const std::optional<ClientViewportRect> area_viewport = viewport && viewport->kind == WorkspaceViewerViewportKind::area
        ? std::optional<ClientViewportRect>{viewport->rect}
        : std::nullopt;
    return nw::toolset::update_area_object_placement(renderer, state.area_object_placement,
        point, area_viewport, state.workspace.active_tab_id(), state.browser.pressed_recent_index, state.shell);
}

bool accept_area_region_point(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    return nw::toolset::accept_area_region_point(renderer, state.area_object_placement,
        point, viewport.rect, state.shell);
}

bool complete_area_region_placement(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport)
{
    return nw::toolset::complete_area_region_placement(renderer, state.area_object_placement,
        point, viewport.rect, state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

void commit_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state)
{
    if (!state.area_object_placement.active()) { return; }
    nw::toolset::commit_area_object_placement(renderer, state.area_object_placement,
        state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

nw::toolset::ProjectResourceDragContext project_resource_drag_context(const ClientApplicationState& state)
{
    return {
        .active_tab_id = state.workspace.active_tab_id(),
        .details_object = state.workbench.object_details.object,
        .inventory_object = state.workbench.inventory_view.creature_inventory.object,
        .surface = state.workbench.object_workbench_surface,
        .object_matches_tab = active_object_matches_tab(state),
        .inventory_matches_tab = active_creature_inventory_matches_tab(state),
    };
}

bool arm_project_blueprint_drag(ClientApplicationState& state, const nw::Resource& resource,
    const std::filesystem::path& source_path, Rml::Vector2f point)
{
    return nw::toolset::arm_project_blueprint_drag(state.project_blueprint_drag,
        project_resource_drag_context(state), resource, source_path, point);
}

bool project_blueprint_drag_context_matches(const ClientApplicationState& state)
{
    return nw::toolset::project_blueprint_drag_context_matches(state.project_blueprint_drag,
        project_resource_drag_context(state));
}

void cancel_project_blueprint_drag(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::cancel_project_blueprint_drag(doc, state.project_blueprint_drag);
}

bool update_project_blueprint_drag(Rml::Context* context, Rml::ElementDocument* doc,
    ClientApplicationState& state, Rml::Vector2f point)
{
    return nw::toolset::update_project_blueprint_drag(context, doc, state.project_blueprint_drag,
        project_resource_drag_context(state), point, state.workbench.inventory_view.creature_inventory_page,
        state.browser.pressed_recent_index, state.shell);
}

void commit_project_blueprint_drag(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    if (!state.project_blueprint_drag.active()) { return; }
    nw::toolset::commit_project_blueprint_drag(doc, state.project_blueprint_drag,
        state.backend, command_context(state, nw::toolset::CommandSource::renderer), state.shell);
}

} // namespace nw::toolset::client_application_detail
