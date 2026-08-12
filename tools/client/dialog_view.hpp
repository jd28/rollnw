#pragma once

#include "dialog_document.hpp"
#include "virtual_list.hpp"

#include <filesystem>
#include <string>

namespace Rml {
class ElementDocument;
}

namespace nw::toolset {

// The workspace has one active dialog surface, so this state is genuinely
// singular. The document rows it owns remain a flat batch for virtual display.
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

} // namespace nw::toolset
