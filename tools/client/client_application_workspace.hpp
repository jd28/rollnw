#pragma once
#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi_Platform_SDL.h>

namespace nw::toolset::client_application_detail {

// Private composition functions. Feature APIs receive only their own states.
void synchronize_client_mutations(ClientRenderer&, ClientApplicationState&, Rml::Context*, Rml::ElementDocument*);

void apply_workspace_tab_scroll(Rml::ElementDocument* doc, ClientApplicationState& state);

void apply_object_workbench_tab_scroll(
    Rml::ElementDocument* doc, ClientApplicationState& state);

void clear_workspace_tab_drag(ClientApplicationState& state);

void set_recent_hover(Rml::ElementDocument* doc, ClientApplicationState& state, int index);

bool render_project_tree_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

void refresh_recent_list(Rml::ElementDocument* doc, ClientApplicationState& state);

void append_workspace_home_markup(std::string& content_markup, ClientApplicationState& state);

bool workspace_home_active(const ClientApplicationState& state);

void refresh_home_area_catalog(ClientApplicationState& state, bool force);

bool sync_home_area_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

void refresh_workspace_content_impl(Rml::ElementDocument* doc, ClientApplicationState& state, bool preserve_controls);

void refresh_workspace_content(Rml::ElementDocument* doc, ClientApplicationState& state);

void refresh_workspace_tabs(Rml::ElementDocument* doc, ClientApplicationState& state);

void refresh_workspace_view(Rml::ElementDocument* doc, ClientApplicationState& state);

std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, ClientApplicationState& state, int frame_width, int frame_height);

void clear_inactive_object(ClientApplicationState& state);

} // namespace nw::toolset::client_application_detail
