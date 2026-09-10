#pragma once

#include "blueprint_edits.hpp"
#include "blueprint_operations.hpp"
#include "blueprint_update_job.hpp"
#include "command_bus.hpp"
#include "creature_body_part_editor.hpp"
#include "item_editor.hpp"
#include "item_editor_data_model.hpp"
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
    void bind(RmlSmallsBridge* bridge, ShellController* shell, WorkspaceState* workspace) noexcept;
    bool initialize();
    bool initialize_item_editor_data_model(Rml::Context& context);
    bool apply_item_editor_pending_focus(Rml::ElementDocument* document);
    void shutdown_item_editor_data_model();

    [[nodiscard]] std::vector<RecentModuleEntry> list_modules(std::string_view query, size_t limit = 16) const;
    [[nodiscard]] std::vector<LoadedAreaEntry> list_areas(std::string_view query) const;
    [[nodiscard]] ProjectTreeResult list_project_tree(std::string_view query) const;
    [[nodiscard]] ProjectModuleSummary project_module_summary() const;
    [[nodiscard]] std::vector<CommandSpec> list_commands(std::string_view query) const;
    [[nodiscard]] bool has_command(std::string_view command_id) const;
    [[nodiscard]] std::filesystem::path current_project_dir() const;
    [[nodiscard]] ObjectHandle module_object() const noexcept;
    [[nodiscard]] uint64_t module_generation() const noexcept;
    [[nodiscard]] bool blueprint_operation_active() const noexcept { return !blueprint_operation_.empty() || blueprint_live_updates_.has_value(); }
    [[nodiscard]] bool blueprint_worker_active() const noexcept { return blueprint_job_.active(); }
    [[nodiscard]] bool blueprint_publication_pending() const noexcept;
    [[nodiscard]] const BlueprintOperationProgress& blueprint_progress() const noexcept { return blueprint_progress_; }
    [[nodiscard]] const std::vector<std::filesystem::path>& blueprint_updated_documents() const noexcept { return blueprint_updated_documents_; }
    std::optional<CommandResult> poll_blueprint_updates(const std::filesystem::path& executable);

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
    enum class BlueprintReferenceScope : uint8_t {
        none,
        current_area,
        module,
    };

    enum class BlueprintUpdatePhase : uint8_t {
        none,
        prepare_live,
        prepare,
        commit,
        restore,
        finalize,
    };

    [[nodiscard]] static std::string_view worker_phase_name(
        BlueprintUpdatePhase phase) noexcept;
    void register_native_commands();
    void register_blueprint_commands();
    void register_blueprint_reference_commands();
    void register_blueprint_command(std::string id, std::string title,
        CommandBus::Handler handler, CommandFlags flags);
    CommandResult scan_blueprint_references(BlueprintReferenceScope scope);
    CommandResult finalize_blueprint_updates();
    bool reload_blueprint_documents(std::span<const std::string> tab_ids, std::string& error);
    CommandResult show_blueprint_form(BlueprintWriteKind kind, ResourceType::type type);
    CommandResult submit_blueprint_form(const CommandInvocation& invocation, CommandContext& context);
    CommandResult commit_blueprint_writes();
    CommandResult open_area_document(std::string resource, std::string title,
        const CommandInvocation& invocation);
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
    CreatureBodyPartEditor creature_body_part_editor_;
    ItemEditor item_editor_;
    ItemEditorDataModel item_editor_data_model_;
    uint64_t data_object_list_generation_ = 0;
    mutable std::vector<LoadedAreaEntry> loaded_areas_;
    std::filesystem::path current_project_dir_;
    ObjectHandle module_object_{};
    uint64_t module_generation_ = 0;

    struct BlueprintForm {
        BlueprintWriteKind kind;
        ResourceType::type type;
        ObjectHandle source;
        uint64_t module_generation;
        CommandPrompt prompt;
    };
    std::optional<BlueprintForm> blueprint_form_;
    std::optional<PreparedBlueprintWrites> blueprint_writes_;
    std::vector<BlueprintWriteResult> blueprint_write_results_;
    Resource blueprint_reference_source_;
    std::filesystem::path blueprint_reference_area_;
    BlueprintReferenceScope blueprint_reference_scope_ = BlueprintReferenceScope::none;
    std::vector<std::string> blueprint_dirty_tabs_;
    BlueprintUpdateJob blueprint_job_;
    std::optional<LiveBlueprintUpdates> blueprint_live_updates_;
    std::filesystem::path blueprint_operation_;
    BlueprintUpdatePhase blueprint_pending_phase_ = BlueprintUpdatePhase::none;
    BlueprintUpdatePhase blueprint_running_phase_ = BlueprintUpdatePhase::none;
    BlueprintOperationProgress blueprint_progress_;
    std::vector<std::filesystem::path> blueprint_updated_documents_;
    uint64_t blueprint_progress_poll_time_ = 0;
};

} // namespace nw::toolset
