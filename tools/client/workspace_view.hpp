#pragma once

#include "command_bus.hpp"
#include "resource_document.hpp"
#include "viewport_rect.hpp"

#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
}

namespace nw::toolset {

struct LoadedAreaEntry;
struct RecentProjectEntry;
struct WorkspaceTab;
enum class WorkspaceTabKind : uint8_t;
struct BrowserViewState;
struct LoadingViewState;
class ToolsetBackend;
class WorkspaceState;

// One displayed tab strip's scroll and captured drag. The actual tab batch and
// its document owners remain WorkspaceState; no DOM pointer is retained here.
struct WorkspaceViewState {
    float workspace_tab_scroll_x = 0.0f;
    bool workspace_tab_scroll_pending = false;
    float workspace_tab_drag_start_x = 0.0f;
    float workspace_tab_drag_start_y = 0.0f;
    std::string workspace_tab_drag_id;
    bool workspace_tab_dragging = false;
};

struct TabScrollStrip {
    // An event targets one unique strip. The helper serves the two real strips
    // (workspace/workbench), rather than manufacturing a batch of UI singletons.
    const char* viewport_id;
    const char* track_id;
    const char* previous_id;
    const char* next_id;
    const char* tab_class;
};

inline constexpr TabScrollStrip kWorkspaceTabScrollStrip{
    "workspace_tabs", "workspace_tab_track", "workspace_tabs_previous",
    "workspace_tabs_next", "workspace_tab"};
inline constexpr TabScrollStrip kObjectWorkbenchTabScrollStrip{
    "object_workbench_tabs", "object_workbench_tab_track", "object_workbench_tabs_previous",
    "object_workbench_tabs_next", "object_workbench_tab"};

// Cold schema 1. Semantic target IDs and copied source identity outlive SDK
// release. One displayed Workspace is a singleton; tab rows keep their owner.
enum class WorkspaceTabClickKind : uint8_t { none,
    close,
    activate,
    close_subtab,
    activate_subtab };
struct WorkspaceTabClick {
    std::string tab_id;
    std::string subtab_id;
    std::string active_tab_id;
    std::string detail;
    ObjectHandle document{};
    WorkspaceTabKind tab_kind{};
    WorkspaceTabClickKind kind = WorkspaceTabClickKind::none;
};
std::optional<WorkspaceTabClick> capture_workspace_tab_click(Rml::ElementDocument* doc,
    Rml::Element* hit, Rml::Vector2f point, const WorkspaceState& workspace);
// Rechecks live semantic target/source identity and consumes once. Dirty state
// is deliberately live: existing backend close/save prompts remain authoritative.
std::optional<CommandInvocation> take_workspace_tab_click_command(WorkspaceTabClick& click,
    const WorkspaceState& workspace);
// true means content-only refresh; false requires the existing full refresh.
bool sync_workspace_tab_click(Rml::ElementDocument* doc, WorkspaceViewState& view,
    const WorkspaceState& workspace, WorkspaceTabClickKind kind, std::string_view tab_id);

struct TabScrollClick {
    bool enabled = false;
    bool forward = false;
    bool pending = true;
};
std::optional<TabScrollClick> capture_tab_scroll_click(Rml::Element* hit,
    const TabScrollStrip& strip, std::string_view button_class);
bool apply_tab_scroll_click(TabScrollClick& click, Rml::ElementDocument* doc,
    const TabScrollStrip& strip, float& scroll_x);

// One displayed area surface; request ownership/cross-editor cancellation stays
// with composition. A matched unknown attribute carries no surface request.
enum class AreaWorkspaceSurface : uint8_t { properties,
    objects,
    tiles };
struct AreaWorkspaceSurfaceClick {
    std::optional<AreaWorkspaceSurface> surface;
};
std::optional<AreaWorkspaceSurfaceClick> capture_area_workspace_surface_click(Rml::Element* hit);

enum class WorkspaceViewerViewportKind : uint8_t { area,
    preview };

struct WorkspaceViewerViewportRequest {
    std::filesystem::path project_dir;
    std::string resource_path;
    uint64_t module_generation = 0;
    WorkspaceViewerViewportKind kind = WorkspaceViewerViewportKind::area;
    ClientViewportRect rect;
};

// Rows carry their own resource identity; indices are only for highlighting.
std::string area_rows_markup(std::span<const LoadedAreaEntry> areas);
std::string recent_projects_markup(std::span<const RecentProjectEntry> projects);

// One displayed viewport is a singleton. Borrow the current tab only for this
// call; append owning escaped resource metadata and the existing empty prompt.
// Other tab kinds append nothing. Surface/workbench composition stays outside.
void append_workspace_viewport_markup(std::string& markup, const WorkspaceTab& tab);

// The workspace has one displayed viewport. Compare before rebuilding its DOM
// so ordinary Details refreshes do not steal focus from an editor.
bool area_viewport_changed(Rml::ElementDocument* document, const WorkspaceTab* tab);

void apply_tab_scroll(Rml::ElementDocument* doc, const TabScrollStrip& strip, float& scroll_x);
void remember_tab_scroll(Rml::ElementDocument* doc, const TabScrollStrip& strip, float& scroll_x);
[[nodiscard]] float tab_scroll_target(Rml::ElementDocument* doc, const TabScrollStrip& strip, bool forward);
[[nodiscard]] Rml::Element* workspace_tab_element_at_point(Rml::ElementDocument* doc,
    std::string_view class_name, Rml::Vector2f point);
[[nodiscard]] size_t workspace_tab_current_index(const std::vector<WorkspaceTab>& tabs,
    std::string_view id, size_t fallback);
[[nodiscard]] size_t workspace_tab_target_index_at_point(Rml::ElementDocument* doc,
    Rml::Vector2f point, const std::vector<WorkspaceTab>& tabs,
    std::string_view dragged_tab_id, size_t fallback);
// One current strip/primary pointer is a singleton. No DOM/event borrow is
// retained. Nonfinite points cancel; unknown source IDs cannot produce a move.
// Matched movable controls retain consumption even with a missing ID.
struct WorkspaceTabDragUpdate {
    std::optional<CommandInvocation> command;
    bool handled = false;
};
bool begin_workspace_tab_drag(Rml::ElementDocument*, WorkspaceViewState&, Rml::Element* hit, Rml::Vector2f point);
WorkspaceTabDragUpdate update_workspace_tab_drag(Rml::ElementDocument*, WorkspaceViewState&,
    const WorkspaceState&, Rml::Vector2f point);
void clear_workspace_tab_drag(WorkspaceViewState& state);
void refresh_workspace_tabs(Rml::ElementDocument* doc, WorkspaceViewState& state,
    const WorkspaceState& workspace);
[[nodiscard]] bool sync_workspace_tab_elements(Rml::ElementDocument* doc, WorkspaceViewState& state,
    const WorkspaceState& workspace);
[[nodiscard]] bool remove_workspace_tab_element(Rml::ElementDocument* doc, WorkspaceViewState& state,
    const WorkspaceState& workspace, std::string_view tab_id);
[[nodiscard]] std::string workspace_tab_kind_class(WorkspaceTabKind kind);
[[nodiscard]] std::string workspace_tab_detail(const WorkspaceTab& tab);
void append_workspace_subtabs_markup(std::string& markup, const WorkspaceTab& tab);

// Opens home markup and closes its main content. Root appends the optional
// workbench and closing surface tag. Recent history validation stays unchanged.
[[nodiscard]] bool append_workspace_home_start_markup(std::string& markup, BrowserViewState& browser,
    const ToolsetBackend& backend, const LoadingViewState& loading, std::string_view version);
[[nodiscard]] std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, const WorkspaceState& workspace,
    const ToolsetBackend& backend, int frame_width, int frame_height);

// One active content surface. Paths and tab are borrowed for this call; loaders
// retain project-path rejection and diagnostics. Markup walks owned row batches.
std::optional<nw::toolset::ResourceDocument> resource_document_for_tab(const std::filesystem::path& project_dir,
    const nw::toolset::WorkspaceTab& active_tab);
void append_resource_document_inspector(std::string& content_markup,
    const nw::toolset::ResourceDocument& document);
void append_missing_resource_document(std::string& content_markup);

} // namespace nw::toolset
