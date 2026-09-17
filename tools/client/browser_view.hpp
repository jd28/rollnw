#pragma once

#include "project.hpp"
#include "toolset_backend.hpp"
#include "virtual_list.hpp"

#include <RmlUi/Core/Types.h>

#include <limits>
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
void refresh_browser_view(Rml::ElementDocument* doc, BrowserViewState& state,
    const ToolsetBackend& backend, const ShellController& shell,
    bool backend_ready, bool selecting_preview_actor);
[[nodiscard]] bool render_project_tree_window(Rml::ElementDocument* doc, BrowserViewState& state, bool force);
void refresh_home_area_catalog(BrowserViewState& state, const ToolsetBackend& backend, bool force);
[[nodiscard]] bool sync_home_area_window(Rml::ElementDocument* doc, BrowserViewState& state,
    bool home_active, bool force);

} // namespace nw::toolset
