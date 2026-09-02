#pragma once

#include "command_bus.hpp"
#include "creature_body_part_editor.hpp"
#include "item_editor.hpp"
#include "project.hpp"
#include "rml_smalls_bridge.hpp"
#include "shell_controller.hpp"
#include "terminal.hpp"
#include "workspace.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

struct EncounterSpawnEdit;
struct SoundResourceEdit;
struct ItemPlacement;
struct StoreItemPlacement;

struct RecentModuleEntry {
    std::string name;
    std::string path;
};

struct LoadedAreaEntry {
    std::string name;
    std::string resref;
    std::string resource;
    std::filesystem::path map_path;
};

class ToolsetBackend {
public:
    using DocumentSaveHandler = std::function<CommandResult(std::string_view tab_id)>;

    void bind(RmlSmallsBridge* bridge, ShellController* shell, WorkspaceState* workspace) noexcept;
    void set_document_save_handler(DocumentSaveHandler handler);
    bool initialize();

    [[nodiscard]] std::vector<RecentModuleEntry> list_modules(std::string_view query, size_t limit = 16) const;
    [[nodiscard]] std::vector<LoadedAreaEntry> list_areas(std::string_view query) const;
    [[nodiscard]] ProjectTreeResult list_project_tree(std::string_view query) const;
    [[nodiscard]] ProjectModuleSummary project_module_summary() const;
    [[nodiscard]] std::vector<CommandSpec> list_commands(std::string_view query) const;
    [[nodiscard]] bool has_command(std::string_view command_id) const;
    [[nodiscard]] std::filesystem::path current_project_dir() const;
    [[nodiscard]] ObjectHandle module_object() const noexcept;
    [[nodiscard]] uint64_t module_generation() const noexcept;

    CommandResult open_module(std::string_view module_path);
    CommandResult open_project(std::string_view project_path);
    CommandResult execute_command(std::string_view command_id, const std::vector<std::string_view>& args, CommandContext context);
    CommandResult execute_command(CommandInvocation invocation, CommandContext context);
    CommandResult place_area_objects(ObjectHandle area,
        std::span<const ObjectHandle> objects,
        CommandContext context);
    CommandResult place_creature_items(ObjectHandle creature,
        std::span<const ItemPlacement> placements,
        CommandContext context);
    CommandResult place_items(ObjectHandle owner,
        std::span<const ItemPlacement> placements,
        CommandContext context);
    CommandResult place_store_items(ObjectHandle store,
        std::span<const StoreItemPlacement> placements,
        CommandContext context);
    CommandResult replace_encounter_spawns(
        EncounterSpawnEdit edit, CommandContext context);
    CommandResult replace_sound_resources(
        SoundResourceEdit edit, CommandContext context);
    [[nodiscard]] TerminalCompletionResult complete_console_command(std::string_view line, size_t cursor_byte_position) const;
    [[nodiscard]] bool is_open_module_dialog_invocation(std::string_view line) const;
    [[nodiscard]] bool is_open_project_dialog_invocation(std::string_view line) const;
    CommandResult console_execute(std::string_view line, CommandContext context);

private:
    void register_native_commands();
    bool refresh_creature_body_part_editor();
    bool refresh_item_editor();
    bool ensure_data_object_editor_lists();
    [[nodiscard]] bool creature_body_part_editor_is_current() const noexcept;
    [[nodiscard]] bool item_editor_is_current() const noexcept;

    RmlSmallsBridge* bridge_ = nullptr;
    ShellController* shell_ = nullptr;
    WorkspaceState* workspace_ = nullptr;
    CommandBus command_bus_;
    TerminalDispatcher terminal_;
    DocumentSaveHandler document_save_handler_;
    CreatureBodyPartEditor creature_body_part_editor_;
    ItemEditor item_editor_;
    uint64_t data_object_list_generation_ = 0;
    mutable std::vector<LoadedAreaEntry> loaded_areas_;
    std::filesystem::path current_project_dir_;
    ObjectHandle module_object_{};
    uint64_t module_generation_ = 0;
};

} // namespace nw::toolset
