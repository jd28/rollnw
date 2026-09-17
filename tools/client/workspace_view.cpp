#include "workspace_view.hpp"
#include "browser_view.hpp"
#include "loading_view.hpp"

#include "project.hpp"
#include "resource_document.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nw::toolset {
namespace {

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

void append_resource_document_diagnostics(std::string& content_markup, const nw::toolset::ResourceDocument& document)
{
    if (document.diagnostics.empty()) {
        return;
    }

    content_markup += "<div class=\"resource_inspector_diagnostics\">";
    for (const auto& diagnostic : document.diagnostics) {
        content_markup += "<div class=\"resource_inspector_diagnostic resource_inspector_diagnostic_";
        content_markup += escape_html(nw::toolset::resource_document_diagnostic_severity_label(diagnostic.severity));
        content_markup += "\"><span class=\"resource_inspector_diagnostic_level\">";
        content_markup += escape_html(nw::toolset::resource_document_diagnostic_severity_label(diagnostic.severity));
        content_markup += "</span><span class=\"resource_inspector_diagnostic_message\">";
        content_markup += escape_html(diagnostic.message);
        content_markup += "</span></div>";
    }
    content_markup += "</div>";
}

void append_resource_document_properties(std::string& content_markup, const nw::toolset::ResourceDocument& document)
{
    std::string current_group;
    bool group_open = false;

    for (const auto& property : document.properties) {
        if (property.group != current_group) {
            if (group_open) {
                content_markup += "</div>";
            }
            current_group = property.group;
            group_open = true;
            content_markup += "<div class=\"resource_property_group\"><div class=\"resource_property_group_title\">";
            content_markup += escape_html(current_group);
            content_markup += "</div>";
        }

        content_markup += "<div class=\"resource_property_row\"><div class=\"resource_property_name\">";
        content_markup += escape_html(property.name);
        content_markup += "</div><div class=\"resource_property_value\">";
        content_markup += escape_html(property.value);
        content_markup += "</div></div>";
    }

    if (group_open) {
        content_markup += "</div>";
    }
}

} // namespace

std::string area_rows_markup(std::span<const LoadedAreaEntry> areas)
{
    std::string markup;
    for (size_t i = 0; i < areas.size(); ++i) {
        const auto& area = areas[i];
        const auto& title = area.name.empty() ? area.resref : area.name;
        markup += "<div class=\"recent_row\"><div class=\"recent_item\" data-key=\"";
        markup += std::to_string(i);
        markup += "\" data-resref=\"" + Rml::StringUtilities::EncodeRml(area.resref);
        markup += "\"><div class=\"recent_title_plain\">" + Rml::StringUtilities::EncodeRml(title) + "</div>";
        markup += "<div class=\"recent_path\">";
        markup += area.resref.empty() ? "(no resref)" : Rml::StringUtilities::EncodeRml(area.resref);
        markup += "</div></div></div>";
    }
    return markup;
}

std::string recent_projects_markup(std::span<const RecentProjectEntry> projects)
{
    if (projects.empty()) {
        return "<div class=\"home_empty\">No recent projects.</div>";
    }
    std::string markup;
    for (size_t i = 0; i < projects.size(); ++i) {
        const auto& project = projects[i];
        const auto index = std::to_string(i);
        markup += "<div class=\"home_project_row\"><div class=\"home_project_item";
        if (!project.error.empty()) markup += " unavailable";
        markup += "\" data-key=\"" + index + "\"><div class=\"home_project_name\">";
        markup += Rml::StringUtilities::EncodeRml(project.name);
        markup += "</div><div class=\"home_project_path\">";
        markup += Rml::StringUtilities::EncodeRml(project.path);
        markup += "</div>";
        if (!project.error.empty()) {
            markup += "<div class=\"home_project_error\">";
            markup += Rml::StringUtilities::EncodeRml(project.error);
            markup += "</div>";
        }
        markup += "</div><button class=\"home_project_remove\" data-key=\"" + index;
        markup += "\" title=\"Remove from recent projects. Project files are not deleted.\">&#215;</button></div>";
    }
    return markup;
}

bool area_viewport_changed(Rml::ElementDocument* document, const WorkspaceTab* tab)
{
    if (!document || !tab || tab->kind != WorkspaceTabKind::area || tab->detail.empty()) {
        return false;
    }
    const auto* viewport = document->GetElementById("workspace_viewer_viewport");
    return !viewport || viewport->GetAttribute<Rml::String>("data-resource", "") != tab->detail;
}

void apply_tab_scroll(Rml::ElementDocument* doc,
    const TabScrollStrip& strip, float& scroll_x)
{
    auto* tabs = find_el(doc, strip.viewport_id);
    auto* previous = find_el(doc, strip.previous_id);
    auto* next = find_el(doc, strip.next_id);
    if (!tabs) {
        scroll_x = 0.0f;
        if (previous) {
            previous->SetClass("disabled", true);
        }
        if (next) {
            next->SetClass("disabled", true);
        }
        return;
    }

    const float content_width = tabs->GetScrollWidth();
    const float viewport_width = tabs->GetClientWidth();
    const float max_scroll = std::max(0.0f, content_width - viewport_width);
    scroll_x = std::clamp(scroll_x, 0.0f, max_scroll);
    tabs->SetScrollLeft(scroll_x);
    scroll_x = tabs->GetScrollLeft();
    constexpr float boundary_epsilon = 0.5f;
    if (previous) {
        previous->SetClass("disabled", scroll_x <= boundary_epsilon);
    }
    if (next) {
        next->SetClass(
            "disabled",
            scroll_x >= max_scroll - boundary_epsilon);
    }
}

void remember_tab_scroll(Rml::ElementDocument* doc,
    const TabScrollStrip& strip, float& scroll_x)
{
    if (auto* tabs = find_el(doc, strip.viewport_id)) {
        scroll_x = tabs->GetScrollLeft();
    }
}

float tab_scroll_target(Rml::ElementDocument* doc,
    const TabScrollStrip& strip, bool forward)
{
    auto* tabs = find_el(doc, strip.viewport_id);
    auto* track = find_el(doc, strip.track_id);
    if (!tabs || !track) {
        return 0.0f;
    }

    const float viewport_width = tabs->GetClientWidth();
    const float max_scroll = std::max(
        0.0f, tabs->GetScrollWidth() - viewport_width);
    const float current = std::clamp(
        tabs->GetScrollLeft(), 0.0f, max_scroll);
    constexpr float boundary_epsilon = 0.5f;

    if (forward) {
        const float visible_right = current + viewport_width;
        const int child_count = track->GetNumChildren();
        for (int i = 0; i < child_count; ++i) {
            auto* child = track->GetChild(i);
            if (!child || !child->IsClassSet(strip.tab_class)) {
                continue;
            }
            const float child_right = child->GetOffsetLeft()
                + child->GetOffsetWidth();
            if (child_right > visible_right + boundary_epsilon) {
                return std::clamp(
                    child_right - viewport_width, 0.0f, max_scroll);
            }
        }
        return max_scroll;
    }

    for (int i = track->GetNumChildren() - 1; i >= 0; --i) {
        auto* child = track->GetChild(i);
        if (!child || !child->IsClassSet(strip.tab_class)) {
            continue;
        }
        const float child_left = child->GetOffsetLeft();
        if (child_left < current - boundary_epsilon) {
            return std::clamp(child_left, 0.0f, max_scroll);
        }
    }
    return 0.0f;
}

size_t workspace_tab_current_index(const std::vector<nw::toolset::WorkspaceTab>& tabs, std::string_view id, size_t fallback)
{
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].id == id) {
            return i;
        }
    }
    return fallback;
}

size_t workspace_tab_locked_prefix_count(const std::vector<nw::toolset::WorkspaceTab>& tabs)
{
    size_t count = 0;
    while (count < tabs.size() && !tabs[count].movable) {
        ++count;
    }
    return count;
}

size_t workspace_tab_target_index_at_point(Rml::ElementDocument* doc,
    Rml::Vector2f point,
    const std::vector<nw::toolset::WorkspaceTab>& tabs,
    std::string_view dragged_tab_id,
    size_t fallback)
{
    auto* track = find_el(doc, "workspace_tab_track");
    if (!track || tabs.empty()) {
        return fallback;
    }

    const size_t locked_prefix = workspace_tab_locked_prefix_count(tabs);
    if (locked_prefix >= tabs.size()) {
        return std::min(fallback, tabs.size() - 1);
    }

    const size_t dragged_index = workspace_tab_current_index(tabs, dragged_tab_id, fallback);
    size_t target = std::clamp(fallback, locked_prefix, tabs.size() - 1);
    const int child_count = track->GetNumChildren();
    for (int i = 0; i < child_count; ++i) {
        auto* child = track->GetChild(i);
        if (!child || !child->IsClassSet("workspace_tab")) {
            continue;
        }
        const std::string index_text = child->GetAttribute<Rml::String>("data-index", "");
        if (index_text.empty()) {
            continue;
        }

        const std::string child_tab_id = child->GetAttribute<Rml::String>("data-tab", "");
        if (child_tab_id == dragged_tab_id) {
            continue;
        }

        const size_t original_index = static_cast<size_t>(std::strtoull(index_text.c_str(), nullptr, 10));
        if (original_index >= tabs.size() || original_index < locked_prefix) {
            continue;
        }

        const size_t index_without_dragged = (dragged_index < original_index) ? original_index - 1 : original_index;
        target = std::max(index_without_dragged, locked_prefix);
        const float midpoint = child->GetAbsoluteLeft() + child->GetOffsetWidth() * 0.5f;
        if (point.x < midpoint) {
            return target;
        }
    }
    return tabs.size() - 1;
}

void clear_workspace_tab_drag(WorkspaceViewState& state)
{
    state.workspace_tab_drag_id.clear();
    state.workspace_tab_dragging = false;
    state.workspace_tab_drag_start_x = 0.0f;
    state.workspace_tab_drag_start_y = 0.0f;
}

std::string workspace_tab_kind_class(nw::toolset::WorkspaceTabKind kind)
{
    switch (kind) {
    case nw::toolset::WorkspaceTabKind::home:
        return "home";
    case nw::toolset::WorkspaceTabKind::module:
        return "module";
    case nw::toolset::WorkspaceTabKind::project:
        return "project";
    case nw::toolset::WorkspaceTabKind::area:
        return "area";
    case nw::toolset::WorkspaceTabKind::preview:
        return "preview";
    case nw::toolset::WorkspaceTabKind::dialog:
        return "dialog";
    case nw::toolset::WorkspaceTabKind::resource:
        return "resource";
    case nw::toolset::WorkspaceTabKind::generic:
        break;
    }
    return "generic";
}

std::string workspace_tab_detail(const nw::toolset::WorkspaceTab& tab)
{
    switch (tab.kind) {
    case nw::toolset::WorkspaceTabKind::home:
        return "Recent Projects";
    case nw::toolset::WorkspaceTabKind::area:
        return !tab.detail.empty() ? tab.detail : std::string{"No area selected"};
    case nw::toolset::WorkspaceTabKind::preview:
        return !tab.detail.empty() ? tab.detail : (tab.id.rfind("preview:", 0) == 0 ? tab.id.substr(8) : tab.id);
    case nw::toolset::WorkspaceTabKind::dialog:
        return !tab.detail.empty() ? tab.detail : (tab.id.rfind("dialog:", 0) == 0 ? tab.id.substr(7) : tab.id);
    case nw::toolset::WorkspaceTabKind::module:
        return tab.id.rfind("module:", 0) == 0 ? tab.id.substr(7) : tab.id;
    case nw::toolset::WorkspaceTabKind::project:
        return tab.id.rfind("project:", 0) == 0 ? tab.id.substr(8) : tab.id;
    case nw::toolset::WorkspaceTabKind::resource:
        return !tab.detail.empty() ? tab.detail : (tab.id.rfind("resource:", 0) == 0 ? tab.id.substr(9) : tab.id);
    case nw::toolset::WorkspaceTabKind::generic:
        break;
    }
    return tab.id;
}

std::string workspace_tab_icon(const nw::toolset::WorkspaceTab& tab)
{
    switch (tab.kind) {
    case nw::toolset::WorkspaceTabKind::module:
        return "mod";
    case nw::toolset::WorkspaceTabKind::project:
        return "prj";
    case nw::toolset::WorkspaceTabKind::area:
        return {};
    case nw::toolset::WorkspaceTabKind::preview:
        return "3d";
    case nw::toolset::WorkspaceTabKind::dialog:
        return "dlg";
    case nw::toolset::WorkspaceTabKind::resource: {
        std::string name = workspace_tab_detail(tab);
        const auto json_suffix = name.rfind(".json");
        if (json_suffix != std::string::npos && json_suffix + 5 == name.size()) {
            name.erase(json_suffix);
        }
        const auto dot = name.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < name.size()) {
            std::string ext = name.substr(dot + 1);
            for (char& ch : ext) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (ext.size() > 4) {
                ext.resize(4);
            }
            return ext;
        }
        return "res";
    }
    case nw::toolset::WorkspaceTabKind::generic:
        return "tab";
    case nw::toolset::WorkspaceTabKind::home:
        break;
    }
    return {};
}

std::string workspace_home_tab_icon_markup()
{
    return "<span class=\"workspace_tab_graphic workspace_tab_home_graphic\">"
           "<span class=\"home_icon_cabinet\"></span>"
           "<span class=\"home_icon_drawer home_icon_drawer_1\"></span>"
           "<span class=\"home_icon_drawer home_icon_drawer_2\"></span>"
           "<span class=\"home_icon_label home_icon_label_1\"></span>"
           "<span class=\"home_icon_label home_icon_label_2\"></span>"
           "<span class=\"home_icon_handle home_icon_handle_1\"></span>"
           "<span class=\"home_icon_handle home_icon_handle_2\"></span>"
           "<span class=\"home_icon_base\"></span>"
           "</span>";
}

std::string workspace_area_tab_icon_markup()
{
    return "<span class=\"workspace_tab_graphic workspace_tab_area_graphic\">"
           "<span class=\"area_icon_panel area_icon_panel_1\"></span>"
           "<span class=\"area_icon_panel area_icon_panel_2\"></span>"
           "<span class=\"area_icon_panel area_icon_panel_3\"></span>"
           "<span class=\"area_icon_path area_icon_path_1\"></span>"
           "<span class=\"area_icon_path area_icon_path_2\"></span>"
           "<span class=\"area_icon_path area_icon_path_3\"></span>"
           "<span class=\"area_icon_dot area_icon_dot_1\"></span>"
           "<span class=\"area_icon_dot area_icon_dot_2\"></span>"
           "</span>";
}

void append_workspace_subtabs_markup(std::string& content_markup, const nw::toolset::WorkspaceTab& active_tab)
{
    if (active_tab.subtabs.empty()) {
        return;
    }

    std::string active_subtab_id;
    if (active_tab.active_subtab_index && *active_tab.active_subtab_index < active_tab.subtabs.size()) {
        active_subtab_id = active_tab.subtabs[*active_tab.active_subtab_index].id;
    }

    content_markup += "<div class=\"workspace_subtabs\">";
    for (const auto& subtab : active_tab.subtabs) {
        content_markup += "<div class=\"workspace_subtab";
        if (subtab.id == active_subtab_id) {
            content_markup += " active";
        }
        if (subtab.closable) {
            content_markup += " closable";
        }
        content_markup += "\" data-tab=\"";
        content_markup += escape_html(active_tab.id);
        content_markup += "\" data-subtab=\"";
        content_markup += escape_html(subtab.id);
        content_markup += "\"><span class=\"workspace_subtab_title\">";
        content_markup += escape_html(subtab.title);
        content_markup += "</span>";
        if (subtab.closable) {
            content_markup += "<div class=\"workspace_subtab_close\" data-tab=\"";
            content_markup += escape_html(active_tab.id);
            content_markup += "\" data-subtab=\"";
            content_markup += escape_html(subtab.id);
            content_markup += "\"><span class=\"workspace_subtab_close_glyph\">x</span></div>";
        }
        content_markup += "</div>";
    }
    content_markup += "</div>";
}

std::string workspace_tab_class(const nw::toolset::WorkspaceTab& tab, std::string_view active_tab_id, const WorkspaceViewState& state)
{
    std::string out = "workspace_tab workspace_tab_";
    out += workspace_tab_kind_class(tab.kind);
    if (tab.id == active_tab_id) {
        out += " active";
    }
    if (tab.closable) {
        out += " closable";
    }
    if (!tab.movable) {
        out += " locked";
    }
    if (tab.dirty) {
        out += " dirty";
    }
    if (state.workspace_tab_dragging && tab.id == state.workspace_tab_drag_id) {
        out += " dragging";
    }
    return out;
}

bool sync_workspace_tab_elements(Rml::ElementDocument* doc, WorkspaceViewState& state, const WorkspaceState& workspace)
{
    auto* track = find_el(doc, "workspace_tab_track");
    if (!track) {
        return false;
    }

    const auto& tabs = workspace.tabs();
    if (track->GetNumChildren() != static_cast<int>(tabs.size())) {
        return false;
    }

    const std::string active_tab_id = workspace.active_tab_id();
    for (size_t tab_index = 0; tab_index < tabs.size(); ++tab_index) {
        auto* child = track->GetChild(static_cast<int>(tab_index));
        if (!child || !child->IsClassSet("workspace_tab")) {
            return false;
        }

        const auto& tab = tabs[tab_index];
        if (child->GetAttribute<Rml::String>("data-tab", "") != tab.id) {
            return false;
        }

        child->SetAttribute("class", workspace_tab_class(tab, active_tab_id, state));
        child->SetAttribute("data-index", std::to_string(tab_index));
        child->SetAttribute("data-movable", tab.movable ? "1" : "0");
    }

    state.workspace_tab_scroll_pending = true;
    return true;
}

bool remove_workspace_tab_element(Rml::ElementDocument* doc, WorkspaceViewState& state, const WorkspaceState& workspace, std::string_view tab_id)
{
    auto* track = find_el(doc, "workspace_tab_track");
    if (!track) {
        return false;
    }

    const int child_count = track->GetNumChildren();
    for (int i = 0; i < child_count; ++i) {
        auto* child = track->GetChild(i);
        if (child && child->IsClassSet("workspace_tab") && child->GetAttribute<Rml::String>("data-tab", "") == tab_id) {
            track->RemoveChild(child).reset();
            state.workspace_tab_scroll_pending = true;
            return sync_workspace_tab_elements(doc, state, workspace);
        }
    }
    return false;
}

void refresh_workspace_tabs(Rml::ElementDocument* doc, WorkspaceViewState& state, const WorkspaceState& workspace)
{
    if (!doc) {
        return;
    }

    remember_tab_scroll(doc, kWorkspaceTabScrollStrip,
        state.workspace_tab_scroll_x);
    const auto& tabs = workspace.tabs();
    const std::string active_tab_id = workspace.active_tab_id();

    std::string tab_markup;
    tab_markup += "<div id=\"workspace_tab_track\">";
    for (size_t tab_index = 0; tab_index < tabs.size(); ++tab_index) {
        const auto& tab = tabs[tab_index];
        tab_markup += "<div class=\"";
        tab_markup += workspace_tab_class(tab, active_tab_id, state);
        tab_markup += "\" data-tab=\"";
        tab_markup += escape_html(tab.id);
        tab_markup += "\" data-index=\"";
        tab_markup += std::to_string(tab_index);
        tab_markup += "\" data-movable=\"";
        tab_markup += tab.movable ? "1" : "0";
        tab_markup += "\">";
        if (const std::string icon = workspace_tab_icon(tab); !icon.empty()) {
            tab_markup += "<span class=\"workspace_tab_icon workspace_tab_icon_";
            tab_markup += workspace_tab_kind_class(tab.kind);
            tab_markup += "\">";
            tab_markup += escape_html(icon);
            tab_markup += "</span>";
        }
        tab_markup += "<span class=\"workspace_tab_title\">";
        if (tab.kind == nw::toolset::WorkspaceTabKind::home) {
            tab_markup += workspace_home_tab_icon_markup();
        } else if (tab.kind == nw::toolset::WorkspaceTabKind::area) {
            tab_markup += workspace_area_tab_icon_markup();
        } else {
            tab_markup += escape_html(tab.title);
        }
        if (tab.dirty && tab.kind != nw::toolset::WorkspaceTabKind::area) {
            tab_markup += " *";
        }
        tab_markup += "</span>";
        if (tab.kind == nw::toolset::WorkspaceTabKind::area) {
            tab_markup += "<span class=\"workspace_tab_dirty\" title=\"Unsaved changes\"></span>";
        }
        if (tab.closable) {
            tab_markup += "<div class=\"workspace_tab_close\" data-tab=\"";
            tab_markup += escape_html(tab.id);
            tab_markup += "\"><span class=\"workspace_tab_close_glyph\">x</span></div>";
        }
        tab_markup += "</div>";
    }
    tab_markup += "</div>";
    if (auto* tab_strip = doc->GetElementById("workspace_tabs")) {
        tab_strip->SetInnerRML(tab_markup);
        state.workspace_tab_scroll_pending = true;
    }
}

Rml::Element* workspace_tab_element_at_point(Rml::ElementDocument* doc, std::string_view class_name, Rml::Vector2f point)
{
    auto* tabs = find_el(doc, "workspace_tabs");
    if (!tabs || !tabs->IsVisible(true)
        || !tabs->IsPointWithinElement(point)) {
        return nullptr;
    }

    const std::string class_text{class_name};
    const auto visit = [&](auto&& self, Rml::Element* element) -> Rml::Element* {
        if (!element || !element->IsVisible(true)) {
            return nullptr;
        }
        if (element->IsClassSet(class_text.c_str()) && element->IsPointWithinElement(point)) {
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

    return visit(visit, tabs);
}

std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, const WorkspaceState& workspace, const ToolsetBackend& backend, int frame_width, int frame_height)
{
    if (!doc || frame_width <= 0 || frame_height <= 0) {
        return std::nullopt;
    }

    const auto* active_tab = workspace.active_tab();
    if (!active_tab
        || (active_tab->kind != nw::toolset::WorkspaceTabKind::area
            && active_tab->kind != nw::toolset::WorkspaceTabKind::preview)
        || active_tab->detail.empty()) {
        return std::nullopt;
    }

    const auto project_dir = backend.current_project_dir();
    if (project_dir.empty()) {
        return std::nullopt;
    }

    auto* viewport_element = doc->GetElementById("workspace_viewer_viewport");
    if (!viewport_element) {
        return std::nullopt;
    }

    const float left_f = viewport_element->GetAbsoluteLeft() + viewport_element->GetClientLeft();
    const float top_f = viewport_element->GetAbsoluteTop() + viewport_element->GetClientTop();
    const float right_f = left_f + viewport_element->GetClientWidth();
    const float bottom_f = top_f + viewport_element->GetClientHeight();

    const int left = std::clamp(static_cast<int>(std::floor(left_f)), 0, frame_width);
    const int top = std::clamp(static_cast<int>(std::floor(top_f)), 0, frame_height);
    const int right = std::clamp(static_cast<int>(std::ceil(right_f)), left, frame_width);
    const int bottom = std::clamp(static_cast<int>(std::ceil(bottom_f)), top, frame_height);
    if (right - left < 8 || bottom - top < 8) {
        return std::nullopt;
    }

    return WorkspaceViewerViewportRequest{
        project_dir,
        active_tab->detail,
        backend.module_generation(),
        active_tab->kind == nw::toolset::WorkspaceTabKind::area
            ? WorkspaceViewerViewportKind::area
            : WorkspaceViewerViewportKind::preview,
        ClientViewportRect{
            left,
            top,
            static_cast<uint32_t>(right - left),
            static_cast<uint32_t>(bottom - top),
        },
    };
}

bool append_workspace_home_start_markup(std::string& content_markup, BrowserViewState& browser,
    const ToolsetBackend& backend, const LoadingViewState& loading, std::string_view version)
{
    const auto project_dir = backend.current_project_dir();
    const nw::ObjectHandle module_object = backend.module_object();
    const bool project_open = !project_dir.empty();
    const bool module_open = module_object.type == nw::ObjectType::module;

    content_markup += "<div class=\"workspace_home_surface\"><div id=\"workspace_home\">";
    content_markup += "<div class=\"workspace_home_content\"><div id=\"workspace_home_header\">";
    content_markup += "<div><div id=\"workspace_home_title\">";
    if (project_open) {
        content_markup += escape_html(nw::toolset::project_display_name(project_dir));
    } else if (module_open) {
        content_markup += escape_html(nw::toolset::live_object_display_name(module_object));
    } else {
        content_markup += "Recent Projects";
    }
    content_markup += "</div>";
    if (project_open) {
        content_markup += "<div id=\"workspace_home_subtitle\">";
        content_markup += escape_html(project_dir.string());
        content_markup += "</div>";
    }
    content_markup += "</div></div>";
    content_markup += "<div class=\"home_app_version\">";
    content_markup += version;
    content_markup += "</div>";
    nw::toolset::append_loading_home_markup(content_markup, loading);

    if (!module_open) {
        nw::toolset::refresh_recent_projects(browser.recent_projects);
        content_markup += "<div id=\"home_project_list\">";
        content_markup += nw::toolset::recent_projects_markup(browser.recent_projects);
        content_markup += "</div>";
    }
    if (module_open) {
        content_markup += "<div id=\"home_area_browser\"><div class=\"home_area_browser_header\">";
        content_markup += "<div class=\"home_section_title\">Areas</div>";
        content_markup += "<div id=\"home_area_count\" class=\"home_area_count\">";
        content_markup += std::to_string(browser.home_areas.size());
        content_markup += "</div></div>";
        content_markup += "<input id=\"home_area_search\" class=\"home_area_search\" type=\"text\" placeholder=\"Filter areas...\" value=\"";
        content_markup += escape_html(browser.home_area_query);
        content_markup += "\"/>";
        content_markup += "<div id=\"home_area_list\" class=\"home_area_list\"></div></div>";
    }
    content_markup += "</div></div>";
    return module_open;
}

std::optional<nw::toolset::ResourceDocument> resource_document_for_tab(const std::filesystem::path& project_dir,
    const nw::toolset::WorkspaceTab& active_tab)
{
    if (active_tab.kind != nw::toolset::WorkspaceTabKind::resource
        && active_tab.kind != nw::toolset::WorkspaceTabKind::preview
        && active_tab.kind != nw::toolset::WorkspaceTabKind::area) {
        return std::nullopt;
    }

    if (project_dir.empty()) {
        return std::nullopt;
    }

    const std::string resource_path = active_tab.detail.empty() ? workspace_tab_detail(active_tab) : active_tab.detail;
    if (resource_path.empty()) {
        return std::nullopt;
    }
    return nw::toolset::load_project_resource_document(project_dir, resource_path);
}

void append_resource_document_inspector(std::string& content_markup,
    const nw::toolset::ResourceDocument& document)
{
    content_markup += "<div class=\"resource_inspector";
    if (!document.ok) {
        content_markup += " error";
    }
    content_markup += "\">";

    content_markup += "<div class=\"resource_inspector_header\"><div class=\"resource_inspector_title\">";
    content_markup += escape_html(document.title.empty() ? std::string{"Resource"} : document.title);
    content_markup += "</div><div class=\"resource_inspector_detail\">";
    content_markup += escape_html(document.detail.empty() ? document.relative_path.generic_string() : document.detail);
    content_markup += "</div><div class=\"resource_inspector_badges\"><span class=\"resource_inspector_badge\">";
    content_markup += escape_html(nw::toolset::resource_document_kind_label(document.kind));
    content_markup += "</span>";
    if (!document.resource_type.empty()) {
        content_markup += "<span class=\"resource_inspector_badge\">";
        content_markup += escape_html(document.resource_type);
        content_markup += "</span>";
    }
    if (!document.format.empty()) {
        content_markup += "<span class=\"resource_inspector_badge\">";
        content_markup += escape_html(document.format);
        content_markup += "</span>";
    }
    content_markup += "</div></div>";

    if (!document.ok) {
        content_markup += "<div class=\"resource_inspector_empty\">";
        content_markup += escape_html(document.message.empty() ? std::string{"Unable to load resource document."} : document.message);
        content_markup += "</div>";
    }

    append_resource_document_diagnostics(content_markup, document);
    append_resource_document_properties(content_markup, document);
    content_markup += "</div>";
}

void append_missing_resource_document(std::string& content_markup)
{
    content_markup += "<div class=\"resource_inspector error\"><div class=\"resource_inspector_header\">";
    content_markup += "<div class=\"resource_inspector_title\">Resource</div>";
    content_markup += "<div class=\"resource_inspector_detail\">No project resource selected</div></div>";
    content_markup += "<div class=\"resource_inspector_empty\">Open a project resource to inspect it.</div></div>";
}

} // namespace nw::toolset
