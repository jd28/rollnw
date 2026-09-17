#pragma once

#include "area_object_editor.hpp"
#include "area_tile_editor.hpp"
#include "browser_view.hpp"
#include "client_input.hpp"
#include "client_metrics.hpp"
#include "command_view.hpp"
#include "dialog_view.hpp"
#include "loading_view.hpp"
#include "object_workbench_view.hpp"
#include "play_preview_view.hpp"
#include "project_resource_drag.hpp"
#include "rml_smalls_bridge.hpp"
#include "rml_smalls_data_model.hpp"
#include "rml_smalls_language_binding.hpp"
#include "runtime_input.hpp"
#include "shell_controller.hpp"
#include "shell_view.hpp"
#include "toolset_backend.hpp"
#include "viewport_pointer_drag.hpp"
#include "workspace_view.hpp"

#include <filesystem>
#include <memory>

// Root composition only. Feature sources must not include this private header.
// One application owns these exact existing feature states; stable addresses and
// field/destruction order are required by backend/SDK/texture bindings. This is
// one process coordinator, not a protocol passed into feature implementations.
namespace nw::toolset::client_application_detail {

struct ClientApplicationState {
    nw::toolset::RmlSmallsBridge smalls;
    nw::toolset::ToolsetBackend backend;
    nw::toolset::ShellController shell;
    nw::toolset::ShellViewState shell_view;
    nw::toolset::PlayPreviewState play_preview;
    nw::toolset::RuntimeInputState runtime_input;
    nw::toolset::WorkspaceState workspace;
    nw::toolset::WorkspaceViewState workspace_view;
    nw::toolset::BrowserViewState browser;
    nw::toolset::CommandViewState command_view;
    nw::toolset::LoadingViewState loading;
    nw::toolset::DialogViewState dialog_view;
    nw::toolset::AreaTileEditorState area_tile_editor;
    AreaWorkspaceSurface area_workspace_surface
        = AreaWorkspaceSurface::properties;
    std::unique_ptr<nw::toolset::RmlSmallsLanguageBinding> rml_smalls_binding;
    std::unique_ptr<nw::toolset::RmlSmallsDataModel> rml_smalls_data_model;
    uint64_t observed_object_mutation_epoch = 0;
    uint64_t observed_area_structure_epoch = 0;
    nw::ObjectHandle stale_area_viewport{};
    bool backend_ready = false;
    std::filesystem::path client_executable;
    nw::toolset::ClientPointerOwner viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
    bool viewer_viewport_focused = false;
    nw::toolset::AreaObjectDragState area_object_drag;
    nw::toolset::AreaObjectPlacementState area_object_placement;
    nw::toolset::ProjectBlueprintDragState project_blueprint_drag;
    nw::toolset::ManagedListReorderState managed_list_reorder;
    ClientViewportDragMode viewer_viewport_drag_mode = ClientViewportDragMode::look;
    Rml::Vector2f viewer_viewport_last_point;
    nw::toolset::ClientMetricsState metrics;
    bool workspace_hover_refresh_pending = false;
    Rml::Vector2f workspace_hover_refresh_point;
    nw::toolset::ObjectWorkbenchViewState workbench;
};

} // namespace nw::toolset::client_application_detail
