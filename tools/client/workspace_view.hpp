#pragma once

#include <span>
#include <string>

namespace Rml {
class ElementDocument;
}

namespace nw::toolset {

struct LoadedAreaEntry;
struct RecentProjectEntry;
struct WorkspaceTab;

// Rows carry their own resource identity; indices are only for highlighting.
std::string area_rows_markup(std::span<const LoadedAreaEntry> areas);
std::string recent_projects_markup(std::span<const RecentProjectEntry> projects);

// The workspace has one displayed viewport. Compare before rebuilding its DOM
// so ordinary Details refreshes do not steal focus from an editor.
bool area_viewport_changed(Rml::ElementDocument* document, const WorkspaceTab* tab);

} // namespace nw::toolset
