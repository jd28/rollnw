#pragma once
#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi_Platform_SDL.h>

namespace nw::toolset::client_application_detail {

// Private composition functions. Feature APIs receive only their own states.
bool sync_area_tile_palette_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

bool begin_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

bool update_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

void cancel_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state);

void commit_area_object_drag(ClientRenderer& renderer, ClientApplicationState& state);

bool add_encounter_spawn_point(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

nw::ObjectHandle active_workspace_area(const ClientApplicationState& state) noexcept;

bool area_tile_editor_action_allowed(const ClientApplicationState& state) noexcept;

std::optional<nw::toolset::AreaTileBrush> selected_area_tile_brush(
    const ClientApplicationState& state, uint8_t pointer_button) noexcept;

bool area_tile_stroke_context_valid(const ClientApplicationState& state) noexcept;

bool synchronize_area_viewport_structure(
    ClientRenderer& renderer, ClientApplicationState& state, bool report_failure);

void clear_area_tile_selection(ClientRenderer& renderer, ClientApplicationState& state);

bool select_area_tiles(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

bool cycle_area_tile_at_point(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

bool handle_area_tile_pointer_down(
    ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    ClientApplicationState& state,
    Rml::Vector2f point,
    const WorkspaceViewerViewportRequest& viewport,
    uint8_t pointer_button);

void cancel_area_tile_stroke(ClientRenderer& renderer, ClientApplicationState& state);

bool cancel_area_tile_action(ClientRenderer& renderer, ClientApplicationState& state);

bool open_area_tile_editor(ClientRenderer& renderer, ClientApplicationState& state);

void close_area_tile_editor(ClientRenderer& renderer, ClientApplicationState& state);

bool set_area_workspace_surface(ClientRenderer& renderer,
    ClientApplicationState& state,
    AreaWorkspaceSurface surface);

bool update_area_tile_cursor(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

void flush_area_tile_cursor_update(ClientRenderer& renderer,
    SDL_Window* window,
    Rml::Context* context,
    Rml::ElementDocument* doc,
    ClientApplicationState& state,
    int frame_width,
    int frame_height);

bool begin_area_tile_stroke(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport, uint8_t pointer_button);

void commit_area_tile_stroke(ClientRenderer& renderer, ClientApplicationState& state);

void arm_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state, nw::Resource resource, Rml::Vector2f point);

void cancel_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state);

bool update_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const std::optional<WorkspaceViewerViewportRequest>& viewport);

bool accept_area_region_point(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

bool complete_area_region_placement(ClientRenderer& renderer, ClientApplicationState& state, Rml::Vector2f point, const WorkspaceViewerViewportRequest& viewport);

void commit_area_object_placement(ClientRenderer& renderer, ClientApplicationState& state);

nw::toolset::ProjectResourceDragContext project_resource_drag_context(const ClientApplicationState& state);

bool arm_project_blueprint_drag(ClientApplicationState& state, const nw::Resource& resource,
    const std::filesystem::path& source_path, Rml::Vector2f point);

bool project_blueprint_drag_context_matches(const ClientApplicationState& state);

void cancel_project_blueprint_drag(Rml::ElementDocument* doc, ClientApplicationState& state);

bool update_project_blueprint_drag(Rml::Context* context, Rml::ElementDocument* doc,
    ClientApplicationState& state, Rml::Vector2f point);

void commit_project_blueprint_drag(Rml::ElementDocument* doc, ClientApplicationState& state);

} // namespace nw::toolset::client_application_detail
