#include "browser_view.hpp"
#include "client_input.hpp"
#include "shell_controller.hpp"
#include "workspace_view.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace nw::toolset {
namespace {

constexpr float kVirtualTreeRowHeightPx = 26.0f;
constexpr size_t kVirtualTreeOverscanRows = 8;
constexpr size_t kInvalidVirtualIndex = std::numeric_limits<size_t>::max();
constexpr int kHomeAreaRowHeightPx = 190;
constexpr int kHomeAreaOverscanRows = 2;
constexpr int kHomeAreaMinimumCardWidthPx = 240;
constexpr int kHomeAreaCardGapPx = 8;
constexpr int kHomeAreaMaximumColumns = 4;

struct VirtualRowWindow {
    size_t start = 0;
    size_t end = 0;
};

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}

std::string escape_html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
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

} // namespace

Rml::Element* recent_item_at_point(Rml::ElementDocument* doc, Rml::Vector2f point)
{
    if (!doc) {
        return nullptr;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        return nullptr;
    }

    if (auto* search = doc->GetElementById("recent_search");
        search && search->IsVisible(true)
        && search->IsPointWithinElement(point)) {
        return nullptr;
    }

    if (!list->IsVisible(true) || !list->IsPointWithinElement(point)) {
        return nullptr;
    }

    return find_recent_item_at(list, point);
}

Rml::Element* find_recent_item_at(Rml::Element* list, Rml::Vector2f point)
{
    if (!list) {
        return nullptr;
    }

    const auto visit = [&](auto&& self, Rml::Element* element) -> Rml::Element* {
        if (!element || !element->IsVisible(true)) {
            return nullptr;
        }
        if (element->IsClassSet("recent_item") && element->IsPointWithinElement(point)) {
            return element;
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            if (auto* found = self(self, element->GetChild(i))) {
                return found;
            }
        }
        return nullptr;
    };

    return visit(visit, list);
}

void set_recent_hover(Rml::ElementDocument* doc, BrowserViewState& state, int hovered_index)
{
    if (!doc) {
        return;
    }
    if (state.hovered_recent_index == hovered_index) {
        return;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        state.hovered_recent_index = hovered_index;
        return;
    }

    list->SetProperty("cursor", hovered_index >= 0 ? "pointer" : "auto");

    const std::string target_key = hovered_index >= 0 ? std::to_string(hovered_index) : std::string();

    const auto visit = [&](auto&& self, Rml::Element* element) -> void {
        if (!element) {
            return;
        }
        if (element->IsClassSet("recent_item")) {
            const bool active = (hovered_index >= 0 && element->GetAttribute<Rml::String>("data-key", "") == target_key);
            element->SetClass("hovered", active);
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            self(self, element->GetChild(i));
        }
    };

    visit(visit, list);
    state.hovered_recent_index = hovered_index;
}

void set_recent_selected(Rml::ElementDocument* doc, BrowserViewState& state, int selected_index)
{
    if (!doc) {
        return;
    }

    auto* list = doc->GetElementById("recent_list");
    if (!list) {
        state.selected_recent_index = selected_index;
        return;
    }

    const std::string target_key = selected_index >= 0 ? std::to_string(selected_index) : std::string();

    const auto visit = [&](auto&& self, Rml::Element* element) -> void {
        if (!element) {
            return;
        }
        if (element->IsClassSet("recent_item")) {
            const bool active = (selected_index >= 0 && element->GetAttribute<Rml::String>("data-key", "") == target_key);
            element->SetClass("selected", active);
        }
        const int child_count = element->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            self(self, element->GetChild(i));
        }
    };

    visit(visit, list);
    state.selected_recent_index = selected_index;
}

void reset_project_tree_render_state(BrowserViewState& state)
{
    state.rendered_project_row_start = kInvalidVirtualIndex;
    state.rendered_project_row_end = kInvalidVirtualIndex;
    state.rendered_project_row_count = 0;
}

void append_project_tree_rows(BrowserViewState& state,
    const nw::toolset::ProjectTreeNode& node,
    int depth,
    bool force_expanded)
{
    const bool is_container = node.is_container();
    const bool collapsed = is_container
        && !force_expanded
        && state.collapsed_project_nodes.find(node.id) != state.collapsed_project_nodes.end();

    auto row = node;
    row.children.clear();
    state.project_rows.push_back(ProjectTreeRow{std::move(row), depth, collapsed});

    if (is_container && !collapsed) {
        for (const auto& child : node.children) {
            append_project_tree_rows(state, child, depth + 1, force_expanded);
        }
    }
}

VirtualRowWindow virtual_row_window_for(Rml::Element* list, size_t row_count)
{
    VirtualRowWindow window;
    if (!list || row_count == 0) {
        return window;
    }

    const float client_height = std::max(list->GetClientHeight(), list->GetOffsetHeight());
    const float scroll_top = std::max(0.0f, list->GetScrollTop());
    const size_t first_visible = std::min(row_count, static_cast<size_t>(scroll_top / kVirtualTreeRowHeightPx));
    const size_t visible_count = static_cast<size_t>(std::ceil(client_height / kVirtualTreeRowHeightPx)) + 1;
    window.start = first_visible > kVirtualTreeOverscanRows ? first_visible - kVirtualTreeOverscanRows : 0;
    window.end = std::min(row_count, first_visible + visible_count + kVirtualTreeOverscanRows);
    return window;
}

void append_project_tree_row_markup(const ProjectTreeRow& row,
    size_t row_index,
    std::string& markup)
{
    const auto& node = row.node;
    const bool is_container = node.is_container();
    const bool collapsed = is_container && row.collapsed;
    markup += "<div class=\"recent_row tree_row\"><div class=\"recent_item tree_item";
    switch (node.kind) {
    case nw::toolset::ProjectTreeNodeKind::directory:
        markup += " tree_directory";
        break;
    case nw::toolset::ProjectTreeNodeKind::area:
        markup += " tree_area";
        break;
    case nw::toolset::ProjectTreeNodeKind::resource:
        markup += " tree_resource";
        break;
    case nw::toolset::ProjectTreeNodeKind::file:
        markup += " tree_file";
        break;
    }
    if (!is_container) {
        markup += " tree_leaf";
    }
    if (collapsed) {
        markup += " collapsed";
    }
    markup += "\" data-key=\"";
    markup += std::to_string(row_index);
    markup += "\" style=\"padding-left: ";
    markup += std::to_string(6 + std::max(0, row.depth) * 12);
    markup += "px;\">";
    markup += "<span class=\"tree_twisty";
    markup += is_container ? (collapsed ? " collapsed" : " expanded") : " leaf";
    markup += "\"></span>";
    markup += "<span class=\"tree_label\">";
    markup += escape_html(node.label);
    markup += "</span>";
    if (!node.resource_type.empty() && node.kind != nw::toolset::ProjectTreeNodeKind::directory) {
        markup += "<span class=\"tree_meta\">";
        markup += escape_html(node.resource_type);
        markup += "</span>";
    }
    markup += "</div></div>";
}

bool render_project_tree_window(Rml::ElementDocument* doc, BrowserViewState& state, bool force)
{
    auto* list = find_el(doc, "recent_list");
    if (!list) {
        return false;
    }

    const size_t row_count = state.project_rows.size();
    const VirtualRowWindow window = virtual_row_window_for(list, row_count);
    if (!force
        && row_count == state.rendered_project_row_count
        && window.start == state.rendered_project_row_start
        && window.end == state.rendered_project_row_end) {
        return false;
    }

    const float scroll_top = list->GetScrollTop();
    std::string markup;
    if (row_count == 0) {
        markup = "<div class=\"nw_list_empty\">No results.</div>";
    } else {
        if (window.start > 0) {
            markup += "<div class=\"tree_spacer\" style=\"height: ";
            markup += std::to_string(static_cast<int>(std::lround(static_cast<float>(window.start) * kVirtualTreeRowHeightPx)));
            markup += "px;\"></div>";
        }

        for (size_t i = window.start; i < window.end; ++i) {
            append_project_tree_row_markup(state.project_rows[i], i, markup);
        }

        if (window.end < row_count) {
            markup += "<div class=\"tree_spacer\" style=\"height: ";
            markup += std::to_string(static_cast<int>(std::lround(static_cast<float>(row_count - window.end) * kVirtualTreeRowHeightPx)));
            markup += "px;\"></div>";
        }
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(scroll_top);
    state.rendered_project_row_start = window.start;
    state.rendered_project_row_end = window.end;
    state.rendered_project_row_count = row_count;
    state.hovered_recent_index = -1;
    state.pressed_recent_index = -1;
    set_recent_selected(doc, state, state.selected_recent_index);
    return true;
}

void refresh_browser_view(Rml::ElementDocument* doc, BrowserViewState& state,
    const ToolsetBackend& backend, const ShellController& shell, bool backend_ready, bool selecting_preview_actor)
{
    if (!doc) {
        return;
    }

    const std::string query = get_input_value(doc, "recent_search");
    state.last_recent_query = query;
    state.project_resource_generation = backend_ready ? nw::kernel::resman().generation() : 0;
    std::string markup;

    state.project_rows.clear();
    bool project_tree_rendered = false;

    if (shell.showing_project_tree) {
        reset_project_tree_render_state(state);
        const auto tree = backend.list_project_tree(query);
        if (tree.ok) {
            const bool force_expanded = !query.empty();
            for (const auto& child : tree.root.children) {
                append_project_tree_rows(state, child, 0, force_expanded);
            }
            if (state.selected_recent_index >= static_cast<int>(state.project_rows.size())) {
                state.selected_recent_index = -1;
            }
            (void)render_project_tree_window(doc, state, true);
            project_tree_rendered = true;
        } else {
            markup = "<div class=\"nw_list_empty\">" + escape_html(tree.message) + "</div>";
        }
    } else if (shell.showing_areas) {
        reset_project_tree_render_state(state);
        markup = nw::toolset::area_rows_markup(backend.list_areas(query));
    } else {
        reset_project_tree_render_state(state);
    }

    if (!project_tree_rendered && markup.empty()) {
        markup = shell.showing_areas
            ? "<div class=\"nw_list_empty\">No areas.</div>"
            : "";
    }

    if (!project_tree_rendered) {
        if (auto* list = doc->GetElementById("recent_list")) {
            list->SetInnerRML(markup);
            state.hovered_recent_index = -1;
            state.pressed_recent_index = -1;
            set_recent_selected(doc, state, state.selected_recent_index);
        }
    }

    if (auto* panel = doc->GetElementById("panel")) {
        panel->SetClass("area_mode", shell.showing_areas);
        panel->SetClass("project_mode", shell.showing_project_tree);
    }
    if (auto* areas_title = doc->GetElementById("title_areas")) {
        areas_title->SetClass("visible", shell.showing_areas);
    }
    if (auto* project_title = doc->GetElementById("title_project")) {
        project_title->SetClass("visible", shell.showing_project_tree);
        if (shell.showing_project_tree) {
            const auto project_dir = backend.current_project_dir();
            const std::string title = selecting_preview_actor
                ? std::string{"Choose Preview Creature"}
                : project_dir.empty()
                ? std::string{"Project"}
                : nw::toolset::project_display_name(project_dir);
            project_title->SetInnerRML(escape_html(title));
        }
    }
}

void refresh_home_area_catalog(BrowserViewState& state, const ToolsetBackend& backend, bool force)
{
    const uint64_t generation = backend.module_generation();
    if (!force && generation == state.home_area_generation) {
        return;
    }

    if (generation != state.home_area_generation) {
        state.home_area_query.clear();
    }

    state.home_areas = backend.list_areas(state.home_area_query);
    state.home_area_generation = generation;
    state.home_area_list.set_row_height(kHomeAreaRowHeightPx);
    state.home_area_list.set_overscan(kHomeAreaOverscanRows);
    state.home_area_list.set_scroll_top(0);
    state.rendered_home_area_count = kInvalidVirtualIndex;
    state.rendered_home_area_columns = 0;
}

std::string rml_file_source(const std::filesystem::path& path)
{
    std::string result = path.generic_string();
    std::replace(result.begin(), result.end(), ':', '|');
    if (path.is_absolute()) {
        result.insert(0, result.starts_with('/') ? "file://" : "file:///");
    }
    return result;
}

void append_home_area_card_markup(const nw::toolset::LoadedAreaEntry& area,
    size_t index,
    std::string& markup)
{
    markup += "<div class=\"home_area_card\" data-key=\"";
    markup += std::to_string(index);
    markup += "\"><div class=\"home_area_map\">";
    if (!area.map_path.empty()) {
        markup += "<img src=\"";
        markup += escape_html(rml_file_source(area.map_path));
        markup += "\"/>";
    } else {
        markup += "<div class=\"home_area_map_missing\">Map unavailable</div>";
    }
    markup += "</div><div class=\"home_area_name\">";
    markup += escape_html(area.name.empty() ? area.resref : area.name);
    markup += "</div><div class=\"home_area_resref\">";
    markup += escape_html(area.resref);
    markup += "</div></div>";
}

bool sync_home_area_window(Rml::ElementDocument* doc, BrowserViewState& state, bool home_active, bool force)
{
    if (!doc || !home_active) {
        return false;
    }
    auto* list = find_el(doc, "home_area_list");
    if (!list) {
        return false;
    }

    const int list_width = std::max(1, static_cast<int>(std::lround(std::max(list->GetClientWidth(), list->GetOffsetWidth()))));
    const int columns = std::clamp(
        (list_width + kHomeAreaCardGapPx)
            / (kHomeAreaMinimumCardWidthPx + kHomeAreaCardGapPx),
        1,
        kHomeAreaMaximumColumns);
    const int logical_rows = static_cast<int>((state.home_areas.size()
                                                  + static_cast<size_t>(columns) - 1)
        / static_cast<size_t>(columns));
    state.home_area_list.set_total_rows(logical_rows);
    state.home_area_list.set_viewport_height(std::max(0,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight())))));
    state.home_area_list.set_scroll_top(std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop()))));
    const auto range = state.home_area_list.compute_range();
    if (!force
        && state.rendered_home_area_count == state.home_areas.size()
        && state.rendered_home_area_columns == columns
        && state.rendered_home_area_range.start == range.start
        && state.rendered_home_area_range.end == range.end) {
        return false;
    }

    const float scroll_top = list->GetScrollTop();
    std::string markup;
    if (state.home_areas.empty()) {
        markup = "<div class=\"home_empty\">No matching areas.</div>";
    } else {
        if (range.top_spacer_px > 0) {
            markup += "<div class=\"home_area_spacer\" style=\"height:";
            markup += std::to_string(range.top_spacer_px);
            markup += "px;\"></div>";
        }
        for (int row = range.start; row < range.end; ++row) {
            markup += "<div class=\"home_area_grid_row\">";
            for (int column = 0; column < columns; ++column) {
                const size_t index = static_cast<size_t>(row * columns + column);
                if (index < state.home_areas.size()) {
                    append_home_area_card_markup(state.home_areas[index], index, markup);
                } else {
                    markup += "<div class=\"home_area_card home_area_card_filler\"></div>";
                }
            }
            markup += "</div>";
        }
        if (range.bottom_spacer_px > 0) {
            markup += "<div class=\"home_area_spacer\" style=\"height:";
            markup += std::to_string(range.bottom_spacer_px);
            markup += "px;\"></div>";
        }
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(scroll_top);
    if (auto* count = find_el(doc, "home_area_count")) {
        count->SetInnerRML(std::to_string(state.home_areas.size()));
    }
    state.rendered_home_area_range = range;
    state.rendered_home_area_count = state.home_areas.size();
    state.rendered_home_area_columns = columns;
    return true;
}

} // namespace nw::toolset
