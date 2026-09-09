#include "workspace_view.hpp"

#include "project.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

namespace nw::toolset {

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

} // namespace nw::toolset
