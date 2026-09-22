#pragma once

#include "project.hpp"
#include "toolset_backend.hpp"
#include "virtual_list.hpp"

#include <RmlUi/Core/Types.h>

#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
}

namespace nw::toolset {

class ShellController;

struct ProjectTreeRow {
    ProjectTreeNode node;
    int depth = 0;
    bool collapsed = false;
};

// One displayed browser owns its rows, filters and render windows. Backend
// results become contiguous rows; DOM indices are highlights, not identities.
struct BrowserViewState {
    std::string last_recent_query;
    uint64_t project_resource_generation = 0;
    int hovered_recent_index = -1;
    int selected_recent_index = -1;
    int pressed_recent_index = -1;
    std::vector<RecentProjectEntry> recent_projects;
    std::string home_area_query;
    std::vector<LoadedAreaEntry> home_areas;
    VirtualListController home_area_list;
    uint64_t home_area_generation = std::numeric_limits<uint64_t>::max();
    VirtualListRange rendered_home_area_range{};
    size_t rendered_home_area_count = std::numeric_limits<size_t>::max();
    int rendered_home_area_columns = 0;
    std::vector<ProjectTreeRow> project_rows;
    std::unordered_set<std::string> collapsed_project_nodes;
    size_t rendered_project_row_start = std::numeric_limits<size_t>::max();
    size_t rendered_project_row_end = std::numeric_limits<size_t>::max();
    size_t rendered_project_row_count = 0;
};

// Borrows are limited to the call. Hit results must be reacquired after any DOM
// callback/replacement; hidden list/search elements never claim the pointer.
[[nodiscard]] Rml::Element* recent_item_at_point(Rml::ElementDocument* doc, Rml::Vector2f point);
[[nodiscard]] Rml::Element* find_recent_item_at(Rml::Element* list, Rml::Vector2f point);
void set_recent_hover(Rml::ElementDocument* doc, BrowserViewState& state, int index);
void set_recent_selected(Rml::ElementDocument* doc, BrowserViewState& state, int index);

enum class BrowserRowClickKind : uint8_t { none,
    open_resource,
    select_area,
    preview_actor };

// Schema revision 1, cold native click. All identities own their bytes across
// SDK callbacks; no row/DOM pointer escapes. One displayed sidebar is singular.
struct BrowserRowClick {
    BrowserRowClickKind kind = BrowserRowClickKind::none;
    int32_t index = -1;
    uint64_t module_generation = 0;
    uint64_t resource_generation = 0;
    std::filesystem::path project_dir;
    std::string query;
    std::string row_id;
    std::filesystem::path resource_path;
    std::string area_resref;
    bool selecting_preview_actor = false;
    bool output_changed = false;
};

// Selects before release, and handles folder refresh/preview preference writes
// at their existing pre-release location. Malformed/unarmed rows do nothing.
BrowserRowClick prepare_browser_row_click(Rml::ElementDocument* doc, Rml::Element* row,
    BrowserViewState& state, const ToolsetBackend& backend, ShellController& shell,
    bool selecting_preview_actor);
// Consume once after SDK release. Changed project/generations/query/mode or
// semantic row rejects; only a matching current identity may invoke an action.
BrowserRowClickKind consume_browser_row_click(BrowserRowClick& click,
    const BrowserViewState& state, const ToolsetBackend& backend,
    const ShellController& shell, bool selecting_preview_actor);

enum class HomeWorkspaceClickKind : uint8_t { none,
    select_area,
    remove_project,
    open_project };

// Schema revision 1: fixed semantic header and owning cold resref/project text.
// One Home click is singular; source catalogs/history are indexed arrays.
struct HomeWorkspaceClick {
    HomeWorkspaceClickKind kind = HomeWorkspaceClickKind::none;
    int32_t index = -1;
    uint64_t module_generation = 0;
    uint64_t resource_generation = 0;
    uint64_t area_generation = 0;
    std::string area_resref;
    RecentProjectEntry project;
};
// Matched malformed keys return a none action. Recent filesystem errors refresh
// before SDK release as before; no DOM or source-row borrow survives return.
std::optional<HomeWorkspaceClick> capture_home_workspace_click(Rml::Element* hit,
    BrowserViewState& state, const ToolsetBackend& backend);
// Consume once after release; changed generation/order/semantic identity rejects.
HomeWorkspaceClickKind consume_home_workspace_click(HomeWorkspaceClick& click,
    const BrowserViewState& state, const ToolsetBackend& backend);

void refresh_browser_view(Rml::ElementDocument* doc, BrowserViewState& state,
    const ToolsetBackend& backend, const ShellController& shell,
    bool backend_ready, bool selecting_preview_actor);
[[nodiscard]] bool render_project_tree_window(Rml::ElementDocument* doc, BrowserViewState& state, bool force);
void refresh_home_area_catalog(BrowserViewState& state, const ToolsetBackend& backend, bool force);
[[nodiscard]] std::string rml_file_source(
    const std::filesystem::path& path);
// The save result owns the path batch for the duration of this synchronous
// UI-thread call. Empty paths are dropped; unloaded textures require no work.
void release_area_map_textures(
    std::span<const std::filesystem::path> paths);
// One displayed home search; hidden/unavailable module retains its query/DOM.
// Missing fields mean empty text; existing catalog rows remain indexed batches.
void refresh_home_area_query(Rml::ElementDocument*, BrowserViewState&, const ToolsetBackend&, bool home_active);
[[nodiscard]] bool sync_home_area_window(Rml::ElementDocument* doc, BrowserViewState& state,
    bool home_active, bool force);

} // namespace nw::toolset
