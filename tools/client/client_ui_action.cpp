#include "client_ui_action.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace nw::toolset {
namespace {

bool valid_context(const ClientUiActionContext& context) noexcept
{
    return (context.map == ClientInputMap::editor || context.map == ClientInputMap::pc)
        && context.surface <= ObjectWorkbenchSurface::store_inventory
        && context.preview <= ClientPreviewPhase::running
        && context.area_surface <= 2 && context.live_objects <= 7
        && context.displayed_object.type <= ObjectType::player
        && context.script_object.type <= ObjectType::player
        && (context.script_area.type == ObjectType::invalid || context.script_area.type == ObjectType::area);
}

bool valid_owner(const ClientUiActionOwner& owner) noexcept
{
    const auto& header = owner.header;
    if (!header.available || header.text_lengths[0] == 0
        || header.tab_kind > static_cast<uint8_t>(WorkspaceTabKind::resource)
        || header.document_object.type > ObjectType::player
        || !valid_context(header.context)) {
        return false;
    }
    uint64_t size = 0;
    for (const auto length : header.text_lengths) {
        size += length;
    }
    return size <= std::numeric_limits<uint32_t>::max() && size == owner.text.size();
}

} // namespace

ClientUiActionOwner capture_client_ui_action_owner(
    const WorkspaceState& workspace, ClientUiActionContext context)
{
    ClientUiActionOwner owner;
    if (!valid_context(context)) { return owner; }
    const auto* tab = workspace.active_tab();
    if (!tab || tab->id.empty() || tab->kind > WorkspaceTabKind::resource
        || tab->document.object().type > ObjectType::player) {
        return owner;
    }
    std::string_view subtab;
    if (tab->active_subtab_index) {
        if (*tab->active_subtab_index >= tab->subtabs.size()) { return owner; }
        subtab = tab->subtabs[*tab->active_subtab_index].id;
        if (subtab.empty()) { return owner; }
    }
    const std::array<std::string_view, 3> strings{tab->id, tab->detail, subtab};
    uint64_t size = 0;
    for (const auto string : strings) {
        if (string.size() > std::numeric_limits<uint32_t>::max() - size) { return owner; }
        size += string.size();
    }
    owner.header.context = context;
    owner.header.document_object = tab->document.object();
    owner.header.tab_kind = static_cast<uint8_t>(tab->kind);
    owner.header.available = true;
    owner.text.reserve(static_cast<size_t>(size));
    for (size_t index = 0; index < strings.size(); ++index) {
        owner.header.text_lengths[index] = static_cast<uint32_t>(strings[index].size());
        owner.text.append(strings[index]);
    }
    return owner;
}

bool match_client_ui_action_owners(std::span<const ClientUiActionOwner> before,
    std::span<const ClientUiActionOwner> after, std::span<bool> matches) noexcept
{
    if (before.size() != after.size() || before.size() != matches.size()) {
        std::fill(matches.begin(), matches.end(), false);
        return false;
    }
    for (size_t index = 0; index < before.size(); ++index) {
        matches[index] = valid_owner(before[index]) && valid_owner(after[index])
            && before[index].header == after[index].header && before[index].text == after[index].text;
    }
    return true;
}

bool same_client_ui_action_owner(const ClientUiActionOwner& before,
    const ClientUiActionOwner& after) noexcept
{
    bool matches = false;
    (void)match_client_ui_action_owners({&before, 1}, {&after, 1}, {&matches, 1});
    return matches;
}

} // namespace nw::toolset
