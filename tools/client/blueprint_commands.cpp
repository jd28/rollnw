#include "toolset_backend.hpp"

#include "resource_document.hpp"

#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <fstream>

namespace nw::toolset {
namespace {

CommandResult failure(std::string message)
{
    return {CommandStatus::rejected, std::move(message), CommandOutputChannel::warn};
}

struct BlueprintChoiceRow {
    CommandPromptChoice choice;
    bool selected = false;
};

bool read_blueprint_choices(std::string_view function,
    std::vector<BlueprintChoiceRow>& output,
    std::string& error)
{
    auto& runtime = kernel::runtime();
    const auto result = runtime.execute_script("nwn1.creature", function);
    auto* rows = result.ok() && result.value.storage == smalls::ValueStorage::heap
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    if (!rows) {
        error = result.ok() ? "The NWN creation catalog is unavailable"
                            : std::string{result.error_message.c_str()};
        return false;
    }
    output.clear();
    output.reserve(rows->size());
    for (size_t index = 0; index < rows->size(); ++index) {
        smalls::Value row;
        if (!rows->get_value(index, row, runtime)
            || row.storage != smalls::ValueStorage::heap) {
            error = "The NWN creation catalog contains an invalid row";
            return false;
        }
        const auto value = runtime.read_struct_field(
            row.data.hptr, row.type_id, "value");
        const auto label = runtime.read_struct_field(
            row.data.hptr, row.type_id, "label");
        const auto selected = runtime.read_struct_field(
            row.data.hptr, row.type_id, "selected");
        if (value.type_id != runtime.int_type()
            || label.type_id != runtime.string_type()
            || label.storage != smalls::ValueStorage::heap
            || selected.type_id != runtime.bool_type()) {
            error = "The NWN creation catalog contains an invalid row";
            return false;
        }
        output.push_back({
            .choice = {std::to_string(value.data.ival),
                label.data.hptr.value == 0
                    ? std::string{}
                    : std::string{runtime.get_string_view(label.data.hptr)}},
            .selected = selected.data.bval,
        });
    }
    return true;
}

bool append_blueprint_choice_field(CommandPrompt& prompt,
    std::string label,
    std::string_view function,
    std::string& error)
{
    std::vector<BlueprintChoiceRow> rows;
    if (!read_blueprint_choices(function, rows, error) || rows.empty()) {
        if (error.empty()) { error = "The NWN creation catalog is empty"; }
        return false;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.choice.label < rhs.choice.label;
    });
    CommandPromptField field{std::move(label), {}, {}, false};
    for (auto& row : rows) {
        if (field.value.empty() && row.selected) {
            field.value = row.choice.value;
        }
        field.choices.push_back(std::move(row.choice));
    }
    if (field.value.empty()) { field.value = field.choices.front().value; }
    prompt.fields.push_back(std::move(field));
    return true;
}

struct CreatedBlueprintFile {
    Resource resource;
    std::filesystem::path target;
    std::string bytes;
};

// History owns only saved files. Undo/redo traverse this cold batch on the
// command thread; no object handles or source-document edits are retained.
CommandResult change_created_blueprint_files(const std::filesystem::path& project,
    std::span<const CreatedBlueprintFile> files, bool restore, CommandContext& context)
{
    if (files.empty()) { return {CommandStatus::noop, {}, CommandOutputChannel::none}; }
    namespace fs = std::filesystem;
    try {
        auto& resources = kernel::resman();
        if (!resources.module_container() || resources.module_format() != ModuleResourceFormat::native_json) {
            return failure("The blueprint's native project is no longer open");
        }
        const auto root = fs::canonical(fs::path{resources.module_container()->path()});
        const auto relative_root = root.lexically_relative(project);
        if (relative_root.empty() || relative_root.is_absolute() || *relative_root.begin() == "..") {
            return failure("The blueprint's project is no longer open");
        }
        for (const auto& file : files) {
            if (fs::is_symlink(file.target) || fs::weakly_canonical(file.target) != file.target) {
                return failure("Blueprint path changed: " + file.target.string());
            }
            if (context.workspace) {
                for (const auto& tab : context.workspace->tabs()) {
                    if (!tab.detail.empty() && fs::weakly_canonical(project / tab.detail) == file.target) {
                        return failure("Close the created blueprint tab before undoing or redoing its creation: " + tab.title);
                    }
                }
            }
            if (restore) {
                const std::array destinations{BlueprintDestination{file.resource, file.target.parent_path()}};
                const auto validated = validate_blueprint_destinations(project, destinations);
                if (!validated[0].error.empty()) { return failure(validated[0].error); }
                // Also catch external files not yet present in the registry.
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (entry.is_regular_file() && Resource::from_path(entry.path(), false) == file.resource) {
                        return failure("Blueprint ResRef already exists: " + file.resource.filename());
                    }
                }
            } else {
                std::ifstream input{file.target, std::ios::binary};
                const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
                if (!input.is_open() || input.bad() || bytes != file.bytes) {
                    return failure("Created blueprint changed; undo will not delete it: " + file.target.string());
                }
            }
        }

        std::vector<size_t> changed;
        std::string error;
        for (size_t index = 0; index < files.size(); ++index) {
            const auto& file = files[index];
            if (restore) {
                const std::array writes{ResourceFileWrite{file.target, file.bytes, ResourceFileWriteMode::create, {}}};
                const auto written = write_resource_files_atomic(writes);
                if (!written[0].written) {
                    error = written[0].error;
                    break;
                }
            } else {
                std::error_code ec;
                if (!fs::remove(file.target, ec)) {
                    error = "Could not delete created blueprint: " + file.target.string() + ": " + ec.message();
                    break;
                }
            }
            changed.push_back(index);
        }
        if (error.empty() && !resources.refresh_module_resources(error)) {
            error = "Blueprint file changed, but resource refresh failed: " + error;
        }
        if (!error.empty()) {
            // Keep history at its previous position when an I/O/refresh failure
            // can be rolled back. Never overwrite a replacement file.
            for (const auto index : changed) {
                const auto& file = files[index];
                if (restore) {
                    std::ifstream input{file.target, std::ios::binary};
                    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
                    input.close();
                    std::error_code ec;
                    if (bytes != file.bytes || !fs::remove(file.target, ec)) {
                        error += "; could not roll back " + file.target.string();
                    }
                } else {
                    const std::array writes{ResourceFileWrite{file.target, file.bytes, ResourceFileWriteMode::create, {}}};
                    if (!write_resource_files_atomic(writes)[0].written) {
                        error += "; could not restore " + file.target.string();
                    }
                }
            }
            return failure(error);
        }
        return {CommandStatus::success, restore ? "Restored created blueprint" : "Deleted created blueprint", CommandOutputChannel::info};
    } catch (const std::exception& ex) {
        return failure(ex.what());
    }
}

std::shared_ptr<CommandUndoAction> blueprint_creation_undo(const PreparedBlueprintWrites& prepared)
{
    auto files = std::make_shared<std::vector<CreatedBlueprintFile>>();
    files->reserve(prepared.rows.size());
    for (const auto& row : prepared.rows) {
        files->push_back({row.request.destination, row.target, row.bytes});
    }
    auto action = std::make_shared<CommandUndoAction>();
    action->label = "Save as New Blueprint";
    action->undo = [project = prepared.project, files](CommandContext& context) {
        return change_created_blueprint_files(project, *files, false, context);
    };
    action->redo = [project = prepared.project, files](CommandContext& context) {
        return change_created_blueprint_files(project, *files, true, context);
    };
    return action;
}

} // namespace

bool ToolsetBackend::blueprint_publication_pending() const noexcept
{
    return std::any_of(blueprint_write_results_.begin(), blueprint_write_results_.end(),
        [](const auto& row) { return row.saved && !row.published; });
}

void ToolsetBackend::register_blueprint_command(std::string id,
    std::string title, CommandBus::Handler handler, CommandFlags flags)
{
    CommandSpec spec;
    spec.id = std::move(id);
    spec.title = std::move(title);
    spec.description = spec.title;
    spec.category = "blueprint";
    spec.scope = CommandScope::workspace;
    spec.flags = flags;
    auto guarded = [handler = std::move(handler)](
                       const CommandInvocation& invocation,
                       CommandContext& context) {
        if (context.play_preview_active) {
            return failure(
                "Exit play preview before working with blueprints");
        }
        try {
            return handler(invocation, context);
        } catch (const std::exception& ex) {
            return failure(ex.what());
        }
    };
    std::string error;
    if (!command_bus_.register_command(
            std::move(spec), std::move(guarded), &error)
        && shell_) {
        shell_->append_output("error", error);
    }
}

void ToolsetBackend::register_blueprint_commands()
{
    register_blueprint_reference_commands();
    const auto add = [&](std::string id, std::string title, CommandBus::Handler handler, CommandFlags flags = CommandFlags::none) {
        register_blueprint_command(std::move(id), std::move(title),
            std::move(handler), flags);
    };
    add("blueprint.new", "New Blueprint...", [this](const CommandInvocation& invocation, CommandContext&) {
        const auto kind = command_arg_string(invocation.args, 0);
        if (kind.empty()) {
            CommandResult result;
            result.prompt = CommandPrompt{
                .id = "blueprint.type",
                .title = "New Blueprint",
                .message = "Choose the blueprint type.",
                .actions = {
                    {"creature", "Creature", "blueprint.new", {"utc"}},
                    {"placeable", "Placeable", "blueprint.new", {"utp"}},
                    {"item", "Item", "blueprint.new", {"uti"}},
                    {"cancel", "Cancel", "blueprint.cancel", {}},
                },
            };
            return result;
        }
        const auto type = kind == "creature" ? ResourceType::utc
            : kind == "placeable"            ? ResourceType::utp
            : kind == "item"                 ? ResourceType::uti
                                             : ResourceType::from_extension(kind);
        return show_blueprint_form(BlueprintWriteKind::create, type);
    });
    add("blueprint.save_as", "Save as New Blueprint...", [this](const CommandInvocation&, CommandContext&) {
        const auto source = bridge_ ? bridge_->active_object() : ObjectHandle{};
        return show_blueprint_form(BlueprintWriteKind::save_as, blueprint_resource_type(source.type));
    });
    add("blueprint.update", "Update Blueprint", [this](const CommandInvocation&, CommandContext&) {
        if (!workspace_ || !bridge_ || current_project_dir_.empty()) { return failure("Open a native project and select an authored object"); }
        const auto source = bridge_->active_object();
        const auto* object = kernel::objects().get_object_base(source);
        if (!object || blueprint_resource_type(source.type) == ResourceType::invalid) { return failure("Select a Creature, Placeable, or Item"); }
        if (auto* tab = workspace_->active_tab(); tab && tab->kind == WorkspaceTabKind::preview && tab->document.object() == source) {
            const std::array<std::string_view, 1> ids{tab->id};
            return save_workspace_documents(*workspace_, current_project_dir_, ids);
        }
        const std::array requests{BlueprintWriteRequest{BlueprintWriteKind::update, source,
            Resource{object->resref, blueprint_resource_type(source.type)}, {}}};
        auto prepared = prepare_blueprint_writes(current_project_dir_, *workspace_, requests);
        if (!prepared.ok()) { return failure(prepared.error); }
        blueprint_writes_ = std::move(prepared);
        blueprint_write_results_.clear();
        CommandResult result;
        result.prompt = CommandPrompt{
            .id = "blueprint.update.review",
            .title = "Update Blueprint",
            .message = "Replace this blueprint with the selected object's authored values? Existing placed instances are updated separately.",
            .detail = blueprint_writes_->rows[0].target.string(),
            .actions = {{"update", "Update Blueprint", "blueprint.commit", {}}, {"cancel", "Cancel", "blueprint.cancel", {}}},
        };
        return result;
    });
    add("blueprint.submit", "Create Blueprint", [this](const CommandInvocation& invocation, CommandContext& context) { return submit_blueprint_form(invocation, context); }, CommandFlags::hidden);
    add("blueprint.commit", "Confirm Blueprint Update", [this](const CommandInvocation&, CommandContext&) { return commit_blueprint_writes(); }, CommandFlags::hidden);
    add("blueprint.refresh", "Retry Blueprint Publication", [this](const CommandInvocation&, CommandContext&) {
        if (!blueprint_writes_ || blueprint_write_results_.empty()) { return failure("No saved blueprint publication is pending"); }
        refresh_blueprint_writes(*workspace_, *blueprint_writes_, blueprint_write_results_);
        return commit_blueprint_writes(); }, CommandFlags::hidden);
    add("blueprint.cancel", "Cancel Blueprint Creation", [this](const CommandInvocation&, CommandContext&) {
        blueprint_form_.reset();
        if (blueprint_write_results_.empty()) { blueprint_writes_.reset(); }
        return CommandResult{CommandStatus::noop, {}, CommandOutputChannel::none}; }, CommandFlags::hidden);
}

CommandResult ToolsetBackend::show_blueprint_form(BlueprintWriteKind kind, ResourceType::type type)
{
    if (!workspace_ || current_project_dir_.empty() || !kernel::resman().module_container()
        || kernel::resman().module_format() != ModuleResourceFormat::native_json) {
        return failure("Open a native project before creating a blueprint");
    }
    const auto object_type = blueprint_object_type(type);
    if (object_type == ObjectType::invalid) { return failure("Choose Creature, Placeable, or Item"); }
    BlueprintForm form{kind, type, {}, module_generation_, {}};
    auto directory = std::filesystem::path{kernel::resman().module_container()->path()} / default_blueprint_directory(type);
    std::string suggested;
    if (kind == BlueprintWriteKind::save_as) {
        form.source = bridge_ ? bridge_->active_object() : ObjectHandle{};
        const auto* source = kernel::objects().get_object_base(form.source);
        if (!source || source->handle().type != object_type) { return failure("The selected blueprint source is unavailable"); }
        suggested = source->resref.string() + "_copy";
        if (const auto* tab = workspace_->active_tab(); tab && tab->kind == WorkspaceTabKind::preview && tab->document.object() == form.source && !tab->detail.empty()) {
            directory = (current_project_dir_ / tab->detail).parent_path();
        }
    }
    auto& prompt = form.prompt;
    prompt.id = "blueprint.destination";
    prompt.title = kind == BlueprintWriteKind::create ? "New " + std::string{placed_area_object_type_label(object_type)} + " Blueprint" : "Save as New Blueprint";
    prompt.message = "ResRefs must be unique for this resource type across the module. Folders organize the saved files.";
    prompt.fields = {{"ResRef", suggested, {}, false}, {"Directory", directory.lexically_relative(current_project_dir_).generic_string(), {}, true}};
    prompt.actions = {{"create", "Create", "blueprint.submit", {}}, {"cancel", "Cancel", "blueprint.cancel", {}}};
    prompt.file_suffix = "." + std::string{ResourceType::to_string(type)} + ".json";
    if (kind == BlueprintWriteKind::create && type == ResourceType::utc) {
        std::string error;
        if (!append_blueprint_choice_field(prompt, "Race",
                "get_blueprint_race_choices", error)
            || !append_blueprint_choice_field(prompt, "Base Class",
                "get_blueprint_class_choices", error)) {
            return failure(error);
        }
    } else if (kind == BlueprintWriteKind::create && type == ResourceType::uti) {
        CommandPromptField field{"Base item type", {}, {}, false};
        const auto& entries = kernel::rules().baseitems.entries;
        for (size_t index = 0; index < entries.size(); ++index) {
            if (!entries[index].valid()) { continue; }
            auto label = kernel::strings().get(entries[index].name);
            if (label.empty()) { label = entries[index].label; }
            field.choices.push_back({std::to_string(index), std::move(label)});
        }
        if (field.choices.empty()) { return failure("The base-item catalog is unavailable"); }
        std::sort(field.choices.begin(), field.choices.end(), [](const auto& lhs, const auto& rhs) { return lhs.label < rhs.label; });
        field.value = field.choices.front().value;
        prompt.fields.push_back(std::move(field));
    }
    blueprint_form_ = std::move(form);
    CommandResult result;
    result.prompt = blueprint_form_->prompt;
    return result;
}

CommandResult ToolsetBackend::submit_blueprint_form(const CommandInvocation& invocation, CommandContext&)
{
    if (!blueprint_form_ || !workspace_ || blueprint_form_->module_generation != module_generation_) { return failure("Blueprint dialog is no longer current"); }
    auto& form = *blueprint_form_;
    if (invocation.args.size() != form.prompt.fields.size()) { return failure("Blueprint dialog has incomplete inputs"); }
    for (size_t index = 0; index < invocation.args.size(); ++index) {
        form.prompt.fields[index].value = command_arg_string(invocation.args, index);
    }
    const auto reject = [&](std::string error) {
        auto result = failure(error);
        form.prompt.detail = std::move(error);
        result.prompt = form.prompt;
        return result;
    };
    std::string resref;
    std::string error;
    if (!validate_blueprint_resref(form.prompt.fields[0].value, resref, error)) { return reject(error); }
    const Resource resource{resref, form.type};
    ObjectHandle source = form.source;
    InitializedBlueprints initialized;
    if (form.kind == BlueprintWriteKind::create) {
        BlueprintCreationRequest request{resource};
        const auto parse_selection = [&](size_t index, int32_t& value,
                                         std::string_view message) {
            const auto& text = form.prompt.fields[index].value;
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{}
                    && parsed.ptr == text.data() + text.size()
                ? std::string{}
                : std::string{message};
        };
        if (form.type == ResourceType::utc) {
            if (auto message = parse_selection(2, request.race,
                    "Choose a race");
                !message.empty()) {
                return reject(std::move(message));
            }
            if (auto message = parse_selection(3, request.class_id,
                    "Choose a class");
                !message.empty()) {
                return reject(std::move(message));
            }
        } else if (form.type == ResourceType::uti) {
            const auto& text = form.prompt.fields[2].value;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), request.base_item);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) { return reject("Choose a base-item type"); }
        }
        initialized = initialize_blueprints(std::span{&request, 1});
        if (!initialized.ok()) { return reject(initialized.error); }
        source = initialized.roots[0].object();
    } else {
        const auto* object = kernel::objects().get_object_base(source);
        if (!object) { return reject("The selected object is no longer available"); }
    }
    const std::array requests{BlueprintWriteRequest{form.kind, source, resource, form.prompt.fields[1].value}};
    auto prepared = prepare_blueprint_writes(current_project_dir_, *workspace_, requests);
    if (!prepared.ok()) { return reject(prepared.error); }
    blueprint_writes_ = std::move(prepared);
    blueprint_write_results_.clear();
    auto result = commit_blueprint_writes();
    if (!result.ok() && blueprint_write_results_.empty()) { return reject(result.message); }
    blueprint_form_.reset();
    return result;
}

CommandResult ToolsetBackend::commit_blueprint_writes()
{
    if (!blueprint_writes_ || !workspace_) { return failure("No prepared blueprint write is pending"); }
    if (blueprint_write_results_.empty()) { blueprint_write_results_ = publish_blueprint_writes(*workspace_, *blueprint_writes_); }
    if (blueprint_write_results_.size() != 1) { return failure("Invalid blueprint publication result"); }
    const auto result = blueprint_write_results_[0];
    if (!result.saved) {
        blueprint_write_results_.clear();
        blueprint_writes_.reset();
        return failure(result.error);
    }
    if (!result.published) {
        auto pending = failure(result.error);
        pending.prompt = CommandPrompt{
            .id = "blueprint.publication",
            .title = "Blueprint saved",
            .message = result.error,
            .detail = result.relative_path.generic_string(),
            .actions = {{"retry", "Retry Resource Refresh", "blueprint.refresh", {}}},
        };
        return pending;
    }
    CommandResult saved{CommandStatus::success, "Saved blueprint: " + result.relative_path.generic_string(), CommandOutputChannel::info};
    if (blueprint_writes_->rows[0].request.kind == BlueprintWriteKind::save_as) {
        saved.undo_action = blueprint_creation_undo(*blueprint_writes_);
    }
    blueprint_write_results_.clear();
    blueprint_writes_.reset();
    return saved;
}

} // namespace nw::toolset
