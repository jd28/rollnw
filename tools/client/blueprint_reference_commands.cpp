#include "toolset_backend.hpp"

#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/component_propset_json.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;
namespace nw::toolset {
namespace {
CommandResult failure(std::string message) { return {CommandStatus::rejected, std::move(message), CommandOutputChannel::warn}; }

nlohmann::json read_document(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    if (!input) { throw std::runtime_error("Cannot read document: " + path.string()); }
    return nlohmann::json::parse(input);
}

} // namespace

std::string_view ToolsetBackend::worker_phase_name(
    BlueprintUpdatePhase phase) noexcept
{
    switch (phase) {
    case BlueprintUpdatePhase::prepare:
        return "prepare";
    case BlueprintUpdatePhase::commit:
        return "commit";
    case BlueprintUpdatePhase::restore:
        return "restore";
    default:
        return {};
    }
}

void ToolsetBackend::register_blueprint_reference_commands()
{
    const auto add = [&](std::string id, std::string title, CommandBus::Handler handler, CommandFlags flags = CommandFlags::hidden) {
        register_blueprint_command(std::move(id), std::move(title),
            std::move(handler), flags);
    };
    add("blueprint.references", "Update Blueprint References...", [this](const CommandInvocation&, CommandContext&) {
        if (!workspace_ || !bridge_ || current_project_dir_.empty()) { return failure("Open a native project and select a blueprint or placed object"); }
        if (blueprint_operation_active()) { return failure("Finish the current blueprint operation first"); }
        const auto source = bridge_->active_object();
        const auto* object = kernel::objects().get_object_base(source);
        if (!object || blueprint_resource_type(source.type) == ResourceType::invalid) {
            return failure("Select a supported blueprint object");
        }
        blueprint_reference_source_ = Resource{object->resref, blueprint_resource_type(source.type)};
        if (!kernel::resman().contains(blueprint_reference_source_)) { return failure("Save this object as a blueprint first"); }
        blueprint_reference_area_.clear();
        for (const auto& tab : workspace_->tabs()) {
            if (tab.kind == WorkspaceTabKind::area && !tab.detail.empty() && kernel::objects().valid(tab.document.object())) { blueprint_reference_area_ = tab.detail; break; }
        }
        CommandPromptField scope{"Scope", blueprint_reference_area_.empty() ? "module" : "area", {}, false};
        if (!blueprint_reference_area_.empty()) { scope.choices.push_back({"area", "Current Area"}); }
        scope.choices.push_back({"module", "Whole Module"});
        CommandResult result;
        result.prompt = CommandPrompt{
            .id = "blueprint.references.scope", .title = "Update Blueprint References",
            .message = "Replace matching instances with the saved blueprint's values, including their inventory and equipment. Placement is preserved. The current area keeps its other edits and becomes unsaved; its existing undo history is cleared.",
            .detail = blueprint_reference_source_.filename() + (blueprint_reference_area_.empty() ? " — no current area is open" : " — current area: " + blueprint_reference_area_.generic_string()),
            .actions = {{"scan", "Find Instances", "blueprint.references.scan", {}}, {"cancel", "Cancel", "blueprint.cancel", {}}},
            .fields = {std::move(scope)},
        };
        return result; }, CommandFlags::none);
    add("blueprint.references.scan", "Find Blueprint Instances", [this](const CommandInvocation& invocation, CommandContext&) {
        const auto value = command_arg_string(invocation.args, 0);
        const auto scope = value == "area" ? BlueprintReferenceScope::current_area
            : value == "module"            ? BlueprintReferenceScope::module
                                           : BlueprintReferenceScope::none;
        return scan_blueprint_references(scope);
    });
    add("blueprint.references.resolve", "Resolve Unsaved Blueprint Documents", [this](const CommandInvocation& invocation, CommandContext&) {
        const auto action = command_arg_string(invocation.args, 0);
        if (blueprint_dirty_tabs_.empty()) { return failure("No unsaved documents are pending"); }
        if (action == "save") {
            std::vector<std::string_view> ids;
            for (const auto& id : blueprint_dirty_tabs_) {
                ids.push_back(id);
            }
            const auto saved = save_workspace_documents(*workspace_, current_project_dir_, ids);
            if (!saved.ok()) { return saved; }
        } else if (action == "discard") {
            std::string error;
            if (!reload_blueprint_documents(blueprint_dirty_tabs_, error)) { return failure(error); }
        } else {
            return failure("Choose Save or Discard");
        }
        blueprint_dirty_tabs_.clear();
        return scan_blueprint_references(blueprint_reference_scope_);
    });
    add("blueprint.references.apply", "Apply Blueprint Replacements", [this](const CommandInvocation&, CommandContext&) {
        if (blueprint_progress_.stage != "ready" || blueprint_job_.active()
            || blueprint_pending_phase_ != BlueprintUpdatePhase::none) {
            return failure("The update is not ready");
        }
        std::string error;
        if (blueprint_live_updates_ && !validate_live_blueprint_updates(*blueprint_live_updates_, error)) { return failure(error); }
        blueprint_pending_phase_ = blueprint_operation_.empty()
            ? BlueprintUpdatePhase::finalize
            : BlueprintUpdatePhase::commit;
        if (blueprint_operation_.empty()) {
            blueprint_running_phase_ = BlueprintUpdatePhase::commit;
        }
        blueprint_progress_.stage = "checking";
        return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none};
    });
    add("blueprint.references.cancel", "Cancel Blueprint Update", [this](const CommandInvocation&, CommandContext&) {
        if (!blueprint_operation_active()) { return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none}; }
        if (blueprint_progress_.stage == "recovery" || blueprint_progress_.stage == "finalizing") { return failure("Restore or finish publishing the saved files before closing this operation"); }
        if (blueprint_job_.active()) {
            std::string error;
            if (!cancel_blueprint_update_operation(blueprint_operation_, error)) { return failure(error); }
            blueprint_progress_.detail = "Cancellation requested; waiting for the current document and any required restoration";
        } else {
            blueprint_pending_phase_ = BlueprintUpdatePhase::none;
            blueprint_running_phase_ = BlueprintUpdatePhase::none;
            blueprint_operation_.clear();
            blueprint_live_updates_.reset();
            blueprint_updated_documents_.clear();
        }
        return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none};
    });
    add("blueprint.references.restore", "Restore Blueprint Reference Update...", [this](const CommandInvocation&, CommandContext&) {
        if (current_project_dir_.empty() || blueprint_operation_active()) { return failure("Open a project and finish its current operation first"); }
        const auto unfinished = find_unfinished_blueprint_operations(current_project_dir_);
        const auto operation = unfinished.empty() ? latest_blueprint_operation(current_project_dir_) : unfinished.front();
        if (operation.empty()) { return failure("No blueprint reference update is available to restore"); }
        blueprint_operation_ = operation;
        blueprint_updated_documents_ = blueprint_operation_documents(operation);
        blueprint_progress_ = {};
        blueprint_progress_.stage = unfinished.empty() ? "restore_ready" : "recovery";
        blueprint_progress_.detail = "Restore the original files from this operation. Files with subsequent edits will be reported as conflicts.";
        return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none}; }, CommandFlags::none);
    add("blueprint.references.restore_apply", "Restore Original Documents", [this](const CommandInvocation&, CommandContext&) {
        if (blueprint_operation_.empty() || blueprint_job_.active()
            || (blueprint_progress_.stage != "restore_ready" && blueprint_progress_.stage != "recovery" && blueprint_progress_.stage != "finalizing")) { return failure("No operation is ready to restore"); }
        const auto files = blueprint_operation_documents(blueprint_operation_);
        for (const auto& tab : workspace_->tabs()) {
            if (tab.dirty && std::find(files.begin(), files.end(), fs::weakly_canonical(current_project_dir_ / tab.detail)) != files.end()) {
                return failure("Save or discard the affected document before restoring: " + tab.title);
            }
        }
        blueprint_live_updates_.reset();
        blueprint_updated_documents_ = files;
        blueprint_pending_phase_ = BlueprintUpdatePhase::restore;
        blueprint_progress_.stage = "restoring";
        return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none};
    });
    add("blueprint.references.retry", "Retry Blueprint Publication", [this](const CommandInvocation&, CommandContext&) {
        if (blueprint_progress_.stage != "finalizing" || blueprint_job_.active()) { return failure("No publication is pending"); }
        return finalize_blueprint_updates();
    });
}

CommandResult ToolsetBackend::scan_blueprint_references(
    BlueprintReferenceScope scope)
{
    if (blueprint_operation_active() || !workspace_ || !blueprint_reference_source_.valid()
        || scope == BlueprintReferenceScope::none
        || (scope == BlueprintReferenceScope::current_area
            && blueprint_reference_area_.empty())) {
        return failure("Select a valid blueprint and update scope");
    }
    blueprint_reference_scope_ = scope;
    blueprint_dirty_tabs_.clear();
    // Only dirty documents which contain matches (on disk or in memory), and the
    // source blueprint, require resolution. Other dirty documents remain open.
    for (const auto& tab : workspace_->tabs()) {
        if (!tab.dirty || tab.kind != WorkspaceTabKind::preview) { continue; }
        const auto resource = Resource::from_path(tab.detail, false);
        const bool source = resource == blueprint_reference_source_;
        if (!source && scope == BlueprintReferenceScope::current_area) {
            continue;
        }
        bool affected = source;
        if (!affected) {
            nlohmann::json value;
            const auto result = object_to_component_propset_json(kernel::objects().get_object_base(tab.document.object()), value, &kernel::runtime(), SerializationProfile::blueprint);
            if (!result) { return failure(result.error); }
            const auto live = collect_blueprint_references(value, resource.type, blueprint_reference_source_);
            if (!live.error.empty()) { return failure(tab.title + ": " + live.error); }
            const auto saved = collect_blueprint_references(read_document(current_project_dir_ / tab.detail), resource.type, blueprint_reference_source_);
            if (!saved.error.empty()) { return failure(tab.title + ": " + saved.error); }
            affected = !live.rows.empty() || !saved.rows.empty();
        }
        if (affected) { blueprint_dirty_tabs_.push_back(tab.id); }
    }
    if (!blueprint_dirty_tabs_.empty()) {
        CommandResult result;
        std::string detail;
        for (const auto& id : blueprint_dirty_tabs_) {
            detail += workspace_->find_tab(id)->title + "\n";
        }
        result.prompt = CommandPrompt{
            .id = "blueprint.references.dirty",
            .title = "Unsaved documents",
            .message = "Save or discard changes in these affected documents before preparing the update?",
            .detail = std::move(detail),
            .actions = {{"save", "Save", "blueprint.references.resolve", {"save"}}, {"discard", "Discard", "blueprint.references.resolve", {"discard"}}, {"cancel", "Cancel", "blueprint.cancel", {}}},
        };
        return result;
    }
    std::string error;
    if (!blueprint_reference_area_.empty()) {
        const auto found = std::find_if(workspace_->tabs().begin(), workspace_->tabs().end(), [&](const auto& tab) {
            return tab.kind == WorkspaceTabKind::area && fs::path{tab.detail} == blueprint_reference_area_;
        });
        if (found == workspace_->tabs().end()) { return failure("The current area is no longer open"); }
        LiveBlueprintUpdates live;
        if (!collect_live_blueprint_references(found->document.object(), blueprint_reference_source_, live, error)) { return failure(error); }
        blueprint_live_updates_ = std::move(live);
    }
    if (scope == BlueprintReferenceScope::module) {
        blueprint_operation_ = create_blueprint_update_operation(current_project_dir_, blueprint_reference_source_, {}, error, blueprint_reference_area_);
        if (blueprint_operation_.empty()) {
            blueprint_live_updates_.reset();
            return failure(error);
        }
    }
    blueprint_pending_phase_ = blueprint_live_updates_
        ? BlueprintUpdatePhase::prepare_live
        : BlueprintUpdatePhase::prepare;
    blueprint_running_phase_ = BlueprintUpdatePhase::none;
    blueprint_progress_ = {};
    blueprint_progress_.stage = "starting";
    blueprint_updated_documents_.clear();
    return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none};
}

bool ToolsetBackend::reload_blueprint_documents(std::span<const std::string> ids, std::string& error)
{
    std::vector<BlueprintUpdateDocument> documents;
    for (const auto& id : ids) {
        const auto* tab = workspace_->find_tab(id);
        if (!tab) {
            error = "Affected document tab no longer exists";
            return false;
        }
        documents.push_back({Resource::from_path(tab->detail, false).type, read_document(current_project_dir_ / tab->detail), {}});
    }
    std::vector<ObjectDocument> loaded;
    if (!load_blueprint_update_documents(documents, loaded, error)) { return false; }
    auto selected = bridge_->active_object();
    auto selected_area = bridge_->active_area();
    for (size_t index = 0; index < ids.size(); ++index) {
        const auto* tab = workspace_->find_tab(ids[index]);
        if (kernel::objects().valid(selected) && selected == tab->document.object()) { selected = loaded[index].object(); }
        if (kernel::objects().valid(selected_area) && selected_area == tab->document.object()) { selected_area = loaded[index].object(); }
        if (tab->kind != WorkspaceTabKind::area) { continue; }
        const auto* previous_area = kernel::objects().get<Area>(tab->document.object());
        if (!previous_area) { continue; }
        std::vector<PlacedAreaObjectRow> previous_members;
        std::vector<PlacedAreaObjectRow> next_members;
        build_placed_area_object_rows(*previous_area, previous_members);
        build_placed_area_object_rows(*kernel::objects().get<Area>(loaded[index].object()), next_members);
        for (size_t member = 0; member < std::min(previous_members.size(), next_members.size()); ++member) {
            if (previous_members[member].object == selected) {
                selected = next_members[member].object;
                break;
            }
        }
    }
    if (!ids.empty()) {
        bridge_->clear_active_object();
        bridge_->clear_active_area();
    }
    for (size_t index = 0; index < ids.size(); ++index) {
        auto* tab = workspace_->find_tab(ids[index]);
        tab->undo_stack.clear();
        tab->redo_stack.clear();
        tab->document = std::move(loaded[index]);
        tab->dirty = false;
    }
    if (kernel::objects().valid(selected)) { bridge_->publish_active_object(selected); }
    if (kernel::objects().valid(selected_area)) { bridge_->publish_active_area(selected_area); }
    if (shell_ && !ids.empty()) { ++shell_->viewer_area_reload_generation; }
    return true;
}

CommandResult ToolsetBackend::finalize_blueprint_updates()
{
    try {
        std::string error;
        if (!blueprint_operation_.empty() && !kernel::resman().refresh_module_resources(error)) { return failure(error); }
        std::vector<std::string> ids;
        for (const auto& tab : workspace_->tabs()) {
            if (tab.detail.empty()) { continue; }
            if (blueprint_live_updates_ && tab.document.object() == blueprint_live_updates_->area) { continue; }
            if (std::find(blueprint_updated_documents_.begin(), blueprint_updated_documents_.end(), fs::weakly_canonical(current_project_dir_ / tab.detail)) != blueprint_updated_documents_.end()) {
                if (tab.dirty) { return failure("An affected document became dirty before publication: " + tab.title); }
                ids.push_back(tab.id);
            }
        }
        if (!reload_blueprint_documents(ids, error)) { return failure(error); }
        if (blueprint_live_updates_ && !blueprint_live_updates_->applied
            && blueprint_running_phase_ != BlueprintUpdatePhase::restore) {
            auto& live = *blueprint_live_updates_;
            auto found = std::find_if(workspace_->tabs().begin(), workspace_->tabs().end(), [&](const auto& tab) { return tab.document.object() == live.area; });
            if (found == workspace_->tabs().end()) { return failure("The live area is no longer open"); }
            if (!validate_live_blueprint_updates(live, error)) { return failure(error); }
            auto* tab = workspace_->find_tab(found->id);
            if (!live.rows.empty()) {
                tab->undo_stack.clear();
                tab->redo_stack.clear();
            }
            auto selection = bridge_->active_object();
            if (!publish_live_blueprint_updates(live, selection, error)) {
                tab->dirty = true;
                return failure(error);
            }
            if (!live.rows.empty()) {
                tab->dirty = true;
                bridge_->clear_active_object();
                if (kernel::objects().valid(selection)) { bridge_->publish_active_object(selection); }
            }
        }
        if (!blueprint_operation_.empty()) {
            if (blueprint_running_phase_ == BlueprintUpdatePhase::commit
                && !finish_blueprint_update_operation(
                    blueprint_operation_, error)) {
                return failure(error);
            }
            blueprint_progress_ = read_blueprint_operation_progress(blueprint_operation_);
            if (blueprint_live_updates_) {
                blueprint_progress_.instances += blueprint_live_updates_->rows.size();
                blueprint_progress_.covered += blueprint_live_updates_->covered;
            }
        }
        blueprint_progress_.stage = "complete";
        blueprint_progress_.detail
            = blueprint_running_phase_ == BlueprintUpdatePhase::restore
            ? "Original documents restored"
            : "Blueprint references updated";
        if (blueprint_live_updates_ && !blueprint_live_updates_->rows.empty()) { blueprint_progress_.detail += "; the current area has unsaved changes"; }
        return {CommandStatus::success, blueprint_progress_.detail, CommandOutputChannel::info};
    } catch (const std::exception& ex) {
        return failure(ex.what());
    }
}

std::optional<CommandResult> ToolsetBackend::poll_blueprint_updates(const fs::path& executable)
{
    if (!blueprint_operation_active()) { return std::nullopt; }
    if (blueprint_pending_phase_ != BlueprintUpdatePhase::none) {
        if (blueprint_pending_phase_ == BlueprintUpdatePhase::prepare_live) {
            auto& live = *blueprint_live_updates_;
            blueprint_progress_.stage = "preparing_live";
            blueprint_progress_.unit = "instances";
            blueprint_progress_.total = live.rows.size();
            blueprint_progress_.instances = live.rows.size();
            blueprint_progress_.covered = live.covered;
            blueprint_progress_.detail = "Preparing replacements in the current area; existing instances remain attached until Apply";
            std::string error;
            // One native load per frame; no concurrent access to the live kernel.
            if (!prepare_live_blueprint_updates(live, 1, error)) {
                blueprint_pending_phase_ = BlueprintUpdatePhase::none;
                blueprint_progress_.stage = "failed";
                blueprint_progress_.error = error;
                return failure(error);
            }
            blueprint_progress_.completed = live.replacements.size();
            if (live.replacements.size() == live.rows.size()) {
                if (!validate_live_blueprint_updates(live, error)) {
                    live.replacements.clear();
                    blueprint_pending_phase_ = BlueprintUpdatePhase::none;
                    blueprint_progress_.stage = "failed";
                    blueprint_progress_.error = error;
                    return failure(error);
                }
                if (blueprint_operation_.empty()) {
                    blueprint_pending_phase_ = BlueprintUpdatePhase::none;
                    blueprint_progress_.stage = "ready";
                    blueprint_progress_.detail = "Matching live instances will be replaced; the area will remain open with unsaved changes";
                    if (!live.rows.empty()) { blueprint_updated_documents_.push_back(fs::weakly_canonical(current_project_dir_ / blueprint_reference_area_)); }
                } else {
                    blueprint_pending_phase_ = BlueprintUpdatePhase::prepare;
                }
            }
            return std::nullopt;
        }
        if (blueprint_pending_phase_ == BlueprintUpdatePhase::finalize) {
            blueprint_pending_phase_ = BlueprintUpdatePhase::none;
            blueprint_progress_.stage = "finalizing";
            const auto result = finalize_blueprint_updates();
            if (!result.ok()) {
                blueprint_progress_.error = result.message;
                if (blueprint_operation_.empty()) { blueprint_progress_.stage = "failed"; }
            }
            return result;
        }
        std::string error;
        blueprint_running_phase_ = std::exchange(
            blueprint_pending_phase_, BlueprintUpdatePhase::none);
        const auto phase = worker_phase_name(blueprint_running_phase_);
        if (phase.empty()
            || !blueprint_job_.start(
                executable, blueprint_operation_, phase, error)) {
            blueprint_progress_.stage
                = blueprint_running_phase_ == BlueprintUpdatePhase::restore
                ? "recovery"
                : "failed";
            blueprint_progress_.error = error;
            return failure(error);
        }
    }
    if (!blueprint_job_.active()) { return std::nullopt; }
    const auto now = SDL_GetTicks();
    if (now - blueprint_progress_poll_time_ < 100) { return std::nullopt; }
    blueprint_progress_poll_time_ = now;
    blueprint_progress_ = read_blueprint_operation_progress(blueprint_operation_);
    const auto exit_code = blueprint_job_.poll();
    if (!exit_code) { return std::nullopt; }
    blueprint_progress_ = read_blueprint_operation_progress(blueprint_operation_);
    if (blueprint_live_updates_) {
        blueprint_progress_.instances += blueprint_live_updates_->rows.size();
        blueprint_progress_.covered += blueprint_live_updates_->covered;
    }
    if (*exit_code != 0) {
        if (blueprint_progress_.error.empty()) {
            blueprint_progress_.error = "Blueprint worker stopped (exit "
                + std::to_string(*exit_code) + "); see "
                + (blueprint_operation_
                    / (std::string{worker_phase_name(blueprint_running_phase_)}
                        + ".log"))
                      .string();
        }
        try {
            const auto unfinished = find_unfinished_blueprint_operations(current_project_dir_);
            blueprint_progress_.stage = std::find(unfinished.begin(), unfinished.end(), blueprint_operation_) != unfinished.end() ? "recovery" : "failed";
        } catch (const std::exception& ex) {
            blueprint_progress_.stage = "recovery";
            blueprint_progress_.error += "\n" + std::string{ex.what()};
        }
        return failure(blueprint_progress_.error);
    }
    try {
        blueprint_updated_documents_ = blueprint_operation_documents(blueprint_operation_);
        if (blueprint_live_updates_ && !blueprint_live_updates_->rows.empty()) {
            blueprint_updated_documents_.push_back(fs::weakly_canonical(current_project_dir_ / blueprint_reference_area_));
            blueprint_progress_.detail += "; current area changes stay unsaved";
        }
    } catch (const std::exception& ex) {
        blueprint_progress_.stage = "recovery";
        blueprint_progress_.error = ex.what();
        return failure(ex.what());
    }
    if (blueprint_running_phase_ != BlueprintUpdatePhase::prepare) {
        blueprint_progress_.stage = "finalizing";
        blueprint_progress_.detail = "Refreshing resources and affected open documents";
        blueprint_pending_phase_ = BlueprintUpdatePhase::finalize;
    }
    return std::nullopt;
}
} // namespace nw::toolset
