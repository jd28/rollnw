#pragma once

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
