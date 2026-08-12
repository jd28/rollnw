#pragma once

#include "command_bus.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

enum class WorkspaceTabKind : uint8_t {
    generic,
    home,
    module,
    project,
    area,
    preview,
    dialog,
    resource,
};

struct WorkspaceSubTab {
    std::string id;
    std::string title;
    bool closable = true;
};

struct WorkspaceTab {
    std::string id;
    std::string title;
    std::string detail;
    WorkspaceTabKind kind = WorkspaceTabKind::generic;
    bool closable = true;
    bool movable = true;
    bool dirty = false;
    std::vector<WorkspaceSubTab> subtabs;
    std::optional<size_t> active_subtab_index;
    std::vector<CommandUndoAction> undo_stack;
    std::vector<CommandUndoAction> redo_stack;
};

enum class WorkspaceCloseStatus : uint8_t {
    closed,
    missing,
    not_closable,
    dirty,
};

struct WorkspaceCloseResult {
    WorkspaceCloseStatus status = WorkspaceCloseStatus::missing;
    std::string tab_id;
    std::string title;
    std::string detail;

    [[nodiscard]] bool closed() const noexcept { return status == WorkspaceCloseStatus::closed; }
    [[nodiscard]] bool needs_save_prompt() const noexcept { return status == WorkspaceCloseStatus::dirty; }
};

class WorkspaceState {
public:
    WorkspaceTab& open_tab(std::string id,
        std::string title = {},
        WorkspaceTabKind kind = WorkspaceTabKind::generic,
        bool closable = true,
        bool movable = true);
    WorkspaceTab& open_or_replace_tab(std::string id,
        std::string title,
        WorkspaceTabKind kind,
        std::string detail,
        bool closable = true,
        bool movable = true);
    void ensure_default_tabs(std::string home_title = "Home", bool reset_area = false);
    WorkspaceCloseResult request_close_tab(std::string_view id, bool force = false);
    bool close_tab(std::string_view id);
    bool set_tab_dirty(std::string_view id, bool dirty);
    bool move_tab(std::string_view id, size_t target_index);
    bool set_active_tab(std::string_view id);
    WorkspaceSubTab* open_subtab(std::string_view tab_id,
        std::string id,
        std::string title = {},
        bool closable = true);
    bool close_subtab(std::string_view tab_id, std::string_view subtab_id);
    bool set_active_subtab(std::string_view tab_id, std::string_view subtab_id);

    [[nodiscard]] WorkspaceTab* active_tab();
    [[nodiscard]] const WorkspaceTab* active_tab() const;
    [[nodiscard]] WorkspaceSubTab* active_subtab();
    [[nodiscard]] const WorkspaceSubTab* active_subtab() const;
    [[nodiscard]] const std::vector<WorkspaceTab>& tabs() const noexcept;
    [[nodiscard]] std::string active_tab_id() const;
    [[nodiscard]] bool has_active_tab() const noexcept;

    void push_undo(CommandUndoAction action);
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    [[nodiscard]] size_t undo_count() const;
    [[nodiscard]] size_t redo_count() const;

    CommandResult undo(CommandContext context);
    CommandResult redo(CommandContext context);

private:
    [[nodiscard]] std::optional<size_t> find_tab_index(std::string_view id) const;
    [[nodiscard]] std::optional<size_t> find_subtab_index(const WorkspaceTab& tab, std::string_view id) const;

    std::vector<WorkspaceTab> tabs_;
    std::optional<size_t> active_index_;
};

} // namespace nw::toolset
