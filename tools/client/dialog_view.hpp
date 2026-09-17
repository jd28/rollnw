#pragma once

#include "dialog_document.hpp"
#include "virtual_list.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
}

namespace nw::toolset {

// The workspace has one active dialog surface, so this state is genuinely
// singular. The document rows it owns remain a flat batch for virtual display.
struct WorkspaceTab;
class WorkspaceState;

struct DialogViewState {
    DialogDocumentSnapshot document;
    VirtualListController list;
    std::string tab_id;
    VirtualListRange rendered_range{};
    int rendered_row_count = 0;
    bool list_configured = false;
    bool rendered = false;
};

void clear_dialog_view(DialogViewState& state);
void load_dialog_view(DialogViewState& state,
    const std::filesystem::path& path,
    std::string tab_id);

[[nodiscard]] std::string dialog_view_markup(const DialogViewState& state);
bool sync_dialog_view(Rml::ElementDocument* document,
    DialogViewState& state,
    bool force);
[[nodiscard]] bool select_dialog_view_row(DialogViewState& state, int row);
// Immediate singleton selection before SDK release. Borrowed hit never escapes;
// nullopt is unmatched, false is matched/rejected, true requests presentation.
std::optional<bool> select_dialog_view_clicked_row(Rml::Element* hit,
    DialogViewState& state, const WorkspaceState& workspace);

// Cached by exact tab ID/source path/status. Non-dialog tabs clear the current
// singleton; unchanged identity keeps selection. Load errors remain explicit.
void ensure_active_dialog_document(DialogViewState& state, const std::filesystem::path& project_dir, const WorkspaceTab* active_tab);

} // namespace nw::toolset
