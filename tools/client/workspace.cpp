#include "workspace.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace nw::toolset {

namespace {

CommandResult workspace_result(CommandStatus status, std::string message, CommandOutputChannel channel = CommandOutputChannel::info)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = channel;
    return result;
}

} // namespace

WorkspaceTab& WorkspaceState::open_tab(std::string id, std::string title, WorkspaceTabKind kind, bool closable, bool movable)
{
    if (const auto existing = find_tab_index(id)) {
        active_index_ = existing;
        if (!title.empty()) {
            tabs_[*existing].title = std::move(title);
        }
        tabs_[*existing].kind = kind;
        tabs_[*existing].closable = closable;
        tabs_[*existing].movable = movable;
        return tabs_[*existing];
    }

    if (title.empty()) {
        title = id;
    }
    WorkspaceTab tab;
    tab.id = std::move(id);
    tab.title = std::move(title);
    tab.kind = kind;
    tab.closable = closable;
    tab.movable = movable;
    tabs_.push_back(std::move(tab));
    active_index_ = tabs_.size() - 1;
    return tabs_.back();
}

WorkspaceTab& WorkspaceState::open_or_replace_tab(
    std::string id, std::string title, WorkspaceTabKind kind, std::string detail, bool closable, bool movable)
{
    if (const auto existing = find_tab_index(id)) {
        active_index_ = existing;
        auto& tab = tabs_[*existing];
        if (!title.empty()) {
            tab.title = std::move(title);
        }
        if (tab.detail != detail) {
            tab.undo_stack.clear();
            tab.redo_stack.clear();
        }
        tab.kind = kind;
        tab.detail = std::move(detail);
        tab.closable = closable;
        tab.movable = movable;
        return tab;
    }

    if (title.empty()) {
        title = id;
    }
    WorkspaceTab tab;
    tab.id = std::move(id);
    tab.title = std::move(title);
    tab.detail = std::move(detail);
    tab.kind = kind;
    tab.closable = closable;
    tab.movable = movable;
    tabs_.push_back(std::move(tab));
    active_index_ = tabs_.size() - 1;
    return tabs_.back();
}

void WorkspaceState::ensure_default_tabs(std::string home_title, bool reset_area)
{
    if (home_title.empty()) {
        home_title = "Home";
    }

    open_tab("home", std::move(home_title), WorkspaceTabKind::home, false, false);
    if (reset_area) {
        open_or_replace_tab("area", "Area", WorkspaceTabKind::area, {}, false, false);
    } else if (const auto area_index = find_tab_index("area")) {
        auto& area = tabs_[*area_index];
        if (area.title.empty()) {
            area.title = "Area";
        }
        area.kind = WorkspaceTabKind::area;
        area.closable = false;
        area.movable = false;
    } else {
        open_or_replace_tab("area", "Area", WorkspaceTabKind::area, {}, false, false);
    }
    set_active_tab("home");
}

bool WorkspaceState::close_tab(std::string_view id)
{
    return request_close_tab(id, true).closed();
}

WorkspaceCloseResult WorkspaceState::request_close_tab(std::string_view id, bool force)
{
    const auto index = find_tab_index(id);
    if (!index) {
        WorkspaceCloseResult result;
        result.status = WorkspaceCloseStatus::missing;
        result.tab_id = std::string{id};
        return result;
    }

    WorkspaceCloseResult result;
    result.tab_id = tabs_[*index].id;
    result.title = tabs_[*index].title;
    result.detail = tabs_[*index].detail;
    if (!tabs_[*index].closable) {
        result.status = WorkspaceCloseStatus::not_closable;
        return result;
    }
    if (tabs_[*index].dirty && !force) {
        result.status = WorkspaceCloseStatus::dirty;
        return result;
    }

    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*index));
    if (tabs_.empty()) {
        active_index_.reset();
    } else if (!active_index_ || *active_index_ == *index) {
        active_index_ = std::min(*index, tabs_.size() - 1);
    } else if (*active_index_ > *index) {
        --(*active_index_);
    }
    result.status = WorkspaceCloseStatus::closed;
    return result;
}

bool WorkspaceState::set_tab_dirty(std::string_view id, bool dirty)
{
    const auto index = find_tab_index(id);
    if (!index) {
        return false;
    }
    tabs_[*index].dirty = dirty;
    return true;
}

bool WorkspaceState::move_tab(std::string_view id, size_t target_index)
{
    const auto index = find_tab_index(id);
    if (!index || !tabs_[*index].movable || tabs_.size() < 2) {
        return false;
    }

    size_t locked_prefix_count = 0;
    while (locked_prefix_count < tabs_.size() && !tabs_[locked_prefix_count].movable) {
        ++locked_prefix_count;
    }

    target_index = std::min(target_index, tabs_.size() - 1);
    target_index = std::max(target_index, locked_prefix_count);
    if (*index == target_index) {
        return true;
    }

    const std::string active_id = active_tab_id();
    WorkspaceTab tab = std::move(tabs_[*index]);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*index));
    target_index = std::min(target_index, tabs_.size());
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(target_index), std::move(tab));
    active_index_ = find_tab_index(active_id);
    return true;
}

bool WorkspaceState::set_active_tab(std::string_view id)
{
    const auto index = find_tab_index(id);
    if (!index) {
        return false;
    }
    active_index_ = index;
    return true;
}

WorkspaceSubTab* WorkspaceState::open_subtab(std::string_view tab_id, std::string id, std::string title, bool closable)
{
    const auto tab_index = find_tab_index(tab_id);
    if (!tab_index || id.empty()) {
        return nullptr;
    }

    auto& tab = tabs_[*tab_index];
    if (const auto subtab_index = find_subtab_index(tab, id)) {
        tab.active_subtab_index = subtab_index;
        if (!title.empty()) {
            tab.subtabs[*subtab_index].title = std::move(title);
        }
        tab.subtabs[*subtab_index].closable = closable;
        return &tab.subtabs[*subtab_index];
    }

    if (title.empty()) {
        title = id;
    }
    WorkspaceSubTab subtab;
    subtab.id = std::move(id);
    subtab.title = std::move(title);
    subtab.closable = closable;
    tab.subtabs.push_back(std::move(subtab));
    tab.active_subtab_index = tab.subtabs.size() - 1;
    return &tab.subtabs.back();
}

bool WorkspaceState::close_subtab(std::string_view tab_id, std::string_view subtab_id)
{
    const auto tab_index = find_tab_index(tab_id);
    if (!tab_index) {
        return false;
    }

    auto& tab = tabs_[*tab_index];
    const auto subtab_index = find_subtab_index(tab, subtab_id);
    if (!subtab_index || !tab.subtabs[*subtab_index].closable) {
        return false;
    }

    tab.subtabs.erase(tab.subtabs.begin() + static_cast<std::ptrdiff_t>(*subtab_index));
    if (tab.subtabs.empty()) {
        tab.active_subtab_index.reset();
    } else if (!tab.active_subtab_index || *tab.active_subtab_index == *subtab_index) {
        tab.active_subtab_index = std::min(*subtab_index, tab.subtabs.size() - 1);
    } else if (*tab.active_subtab_index > *subtab_index) {
        --(*tab.active_subtab_index);
    }
    return true;
}

bool WorkspaceState::set_active_subtab(std::string_view tab_id, std::string_view subtab_id)
{
    const auto tab_index = find_tab_index(tab_id);
    if (!tab_index) {
        return false;
    }

    auto& tab = tabs_[*tab_index];
    const auto subtab_index = find_subtab_index(tab, subtab_id);
    if (!subtab_index) {
        return false;
    }

    active_index_ = tab_index;
    tab.active_subtab_index = subtab_index;
    return true;
}

WorkspaceTab* WorkspaceState::active_tab()
{
    if (!active_index_ || *active_index_ >= tabs_.size()) {
        return nullptr;
    }
    return &tabs_[*active_index_];
}

const WorkspaceTab* WorkspaceState::active_tab() const
{
    if (!active_index_ || *active_index_ >= tabs_.size()) {
        return nullptr;
    }
    return &tabs_[*active_index_];
}

WorkspaceSubTab* WorkspaceState::active_subtab()
{
    auto* tab = active_tab();
    if (!tab || !tab->active_subtab_index || *tab->active_subtab_index >= tab->subtabs.size()) {
        return nullptr;
    }
    return &tab->subtabs[*tab->active_subtab_index];
}

const WorkspaceSubTab* WorkspaceState::active_subtab() const
{
    const auto* tab = active_tab();
    if (!tab || !tab->active_subtab_index || *tab->active_subtab_index >= tab->subtabs.size()) {
        return nullptr;
    }
    return &tab->subtabs[*tab->active_subtab_index];
}

const std::vector<WorkspaceTab>& WorkspaceState::tabs() const noexcept
{
    return tabs_;
}

std::string WorkspaceState::active_tab_id() const
{
    if (const auto* tab = active_tab()) {
        return tab->id;
    }
    return {};
}

bool WorkspaceState::has_active_tab() const noexcept
{
    return active_index_.has_value() && *active_index_ < tabs_.size();
}

void WorkspaceState::push_undo(CommandUndoAction action)
{
    auto* tab = active_tab();
    if (!tab || !action.undo || !action.redo) {
        return;
    }

    tab->undo_stack.push_back(std::move(action));
    tab->redo_stack.clear();
}

bool WorkspaceState::can_undo() const
{
    const auto* tab = active_tab();
    return tab && !tab->undo_stack.empty();
}

bool WorkspaceState::can_redo() const
{
    const auto* tab = active_tab();
    return tab && !tab->redo_stack.empty();
}

size_t WorkspaceState::undo_count() const
{
    const auto* tab = active_tab();
    return tab ? tab->undo_stack.size() : 0;
}

size_t WorkspaceState::redo_count() const
{
    const auto* tab = active_tab();
    return tab ? tab->redo_stack.size() : 0;
}

CommandResult WorkspaceState::undo(CommandContext context)
{
    auto* tab = active_tab();
    if (!tab) {
        return workspace_result(CommandStatus::noop, "No active workspace tab", CommandOutputChannel::warn);
    }
    if (tab->undo_stack.empty()) {
        return workspace_result(CommandStatus::noop, "Nothing to undo", CommandOutputChannel::info);
    }

    auto action = std::move(tab->undo_stack.back());
    tab->undo_stack.pop_back();

    context.workspace = this;
    context.active_tab_id = tab->id;
    context.record_undo = false;
    CommandResult result = action.undo(context);
    if (result.ok()) {
        tab->redo_stack.push_back(std::move(action));
        return result;
    }

    tab->undo_stack.push_back(std::move(action));
    return result;
}

CommandResult WorkspaceState::redo(CommandContext context)
{
    auto* tab = active_tab();
    if (!tab) {
        return workspace_result(CommandStatus::noop, "No active workspace tab", CommandOutputChannel::warn);
    }
    if (tab->redo_stack.empty()) {
        return workspace_result(CommandStatus::noop, "Nothing to redo", CommandOutputChannel::info);
    }

    auto action = std::move(tab->redo_stack.back());
    tab->redo_stack.pop_back();

    context.workspace = this;
    context.active_tab_id = tab->id;
    context.record_undo = false;
    CommandResult result = action.redo(context);
    if (result.ok()) {
        tab->undo_stack.push_back(std::move(action));
        return result;
    }

    tab->redo_stack.push_back(std::move(action));
    return result;
}

std::optional<size_t> WorkspaceState::find_tab_index(std::string_view id) const
{
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<size_t> WorkspaceState::find_subtab_index(const WorkspaceTab& tab, std::string_view id) const
{
    for (size_t i = 0; i < tab.subtabs.size(); ++i) {
        if (tab.subtabs[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace nw::toolset
