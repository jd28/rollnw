#include "toolset_backend.hpp"

#include "area_creation.hpp"
#include "object_document.hpp"
#include "resource_document.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>

namespace nw::toolset {
namespace {

CommandResult failure(std::string message)
{
    return {CommandStatus::rejected, std::move(message),
        CommandOutputChannel::warn};
}

bool parse_dimension(std::string_view text, int32_t& output)
{
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), output);
    return parsed.ec == std::errc{}
    && parsed.ptr == text.data() + text.size();
}

bool path_is_inside(const std::filesystem::path& outer,
    const std::filesystem::path& inner)
{
    const auto relative = inner.lexically_relative(outer);
    return !relative.empty() && !relative.is_absolute()
        && *relative.begin() != "..";
}

bool read_file(const std::filesystem::path& path, std::string& output)
{
    std::ifstream input{path, std::ios::binary};
    output.assign(std::istreambuf_iterator<char>{input}, {});
    return input.is_open() && !input.bad();
}

struct AreaDeleteTarget {
    std::filesystem::path target;
    std::filesystem::path relative;
    std::filesystem::path map;
};

bool resolve_area_delete_target(const std::filesystem::path& project,
    std::string_view detail, AreaDeleteTarget& output, std::string& error)
{
    namespace fs = std::filesystem;
    auto& resources = kernel::resman();
    if (project.empty() || detail.empty() || !resources.module_container()
        || resources.module_format() != ModuleResourceFormat::native_json) {
        error = "Area deletion requires an active native project area";
        return false;
    }

    const fs::path input{detail};
    if (input.is_absolute()) {
        error = "Area path must be relative to the active project";
        return false;
    }

    std::error_code ec;
    const auto canonical_project = fs::canonical(project, ec);
    if (ec) {
        error = "Failed to resolve the active project: " + ec.message();
        return false;
    }
    const auto root = fs::canonical(
        fs::path{resources.module_container()->path()}, ec);
    if (ec || !path_is_inside(canonical_project, root)) {
        error = "Active module resources are outside this project";
        return false;
    }

    const auto unresolved = canonical_project / input;
    const auto status = fs::symlink_status(unresolved, ec);
    if (ec || status.type() == fs::file_type::symlink) {
        error = "Area deletion does not follow symbolic links";
        return false;
    }
    const auto target = fs::canonical(unresolved, ec);
    if (ec || !fs::is_regular_file(target, ec) || ec
        || !path_is_inside(root, target)) {
        error = "Active area file is unavailable inside the module resource root";
        return false;
    }

    const auto resource = Resource::from_path(target, false);
    if (!resource.valid() || resource.type != ResourceType::caf
        || !resources.contains(resource)) {
        error = "Active document is not a published native Area resource";
        return false;
    }

    output.target = target;
    output.relative = target.lexically_relative(canonical_project);
    output.map = project_area_map_path(
        canonical_project, resource.resref.view());
    return true;
}

std::vector<CommandPromptChoice> area_tileset_choices()
{
    std::vector<CommandPromptChoice> choices;
    for (const auto& [resref, tileset] : kernel::tilesets().tileset_map_) {
        AreaTile ground;
        if (!kernel::resman().contains(
                Resource{resref, ResourceType::set})
            || !canonical_area_ground_tile(tileset, ground)) {
            continue;
        }
        std::string label;
        if (tileset.strref != std::numeric_limits<uint32_t>::max()) {
            label = kernel::strings().get(tileset.strref);
            if (label.starts_with("Bad Strref")) { label.clear(); }
        }
        if (label.empty()) { label = tileset.name; }
        if (label.empty()) { label = resref; }
        choices.push_back({resref, std::move(label)});
    }
    std::ranges::sort(choices, [](const auto& lhs, const auto& rhs) {
        if (lhs.label != rhs.label) { return lhs.label < rhs.label; }
        return lhs.value < rhs.value;
    });
    return choices;
}

} // namespace

void ToolsetBackend::register_area_commands()
{
    const auto add = [this](CommandSpec spec, CommandBus::Handler handler) {
        std::string error;
        if (!command_bus_.register_command(
                std::move(spec), std::move(handler), &error)
            && shell_) {
            shell_->append_output("warn", error);
        }
    };
    add(CommandSpec{
            "area.new",
            "New Area...",
            "Create a blank tiled area in the active project",
            "area",
            {"new_area"},
            CommandScope::workspace,
            CommandFlags::none,
            {},
            "area.new",
        },
        [this](const CommandInvocation&, CommandContext&) {
            return show_new_area_form();
        });
    add(CommandSpec{
            "area.create",
            "Create Area",
            "Create a blank tiled area",
            "area",
            {},
            CommandScope::workspace,
            CommandFlags::hidden,
            {},
            "area.create <resref> <directory> <name> <tileset> <width> <height>",
        },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            return submit_new_area_form(invocation, context);
        });
    add(CommandSpec{
            "area.cancel",
            "Cancel Area Creation",
            "Close the new area form",
            "area",
            {},
            CommandScope::workspace,
            CommandFlags::hidden,
            {},
            "area.cancel",
        },
        [](const CommandInvocation&, CommandContext&) {
            return CommandResult{
                CommandStatus::noop, {}, CommandOutputChannel::none};
        });
    add(CommandSpec{
            "area.delete",
            "Delete Area...",
            "Permanently delete the active Area from the project",
            "area",
            {"delete_area"},
            CommandScope::workspace,
            CommandFlags::none,
            {},
            "area.delete",
        },
        [this](const CommandInvocation& invocation, CommandContext&) {
            return delete_current_area(invocation);
        });
}

CommandResult ToolsetBackend::show_new_area_form()
{
    if (!workspace_ || current_project_dir_.empty()
        || !kernel::resman().module_container()
        || kernel::resman().module_format()
            != ModuleResourceFormat::native_json) {
        return failure("Open a native project before creating an area");
    }
    auto choices = area_tileset_choices();
    if (choices.empty()) {
        return failure("No loaded tileset can provide a flat default ground tile");
    }
    namespace fs = std::filesystem;
    const fs::path root{kernel::resman().module_container()->path()};
    const auto default_directory = fs::is_directory(root / "areas")
        ? root / "areas"
        : root;

    std::string selected_tileset;
    if (const auto* tab = workspace_->find_tab("area")) {
        const auto* area = kernel::objects().get<Area>(tab->document.object());
        if (area) { selected_tileset = area->tileset_resref.string(); }
    }
    if (std::ranges::none_of(choices, [&](const auto& choice) {
            return choice.value == selected_tileset;
        })) {
        selected_tileset = choices.front().value;
    }

    CommandPrompt prompt;
    prompt.id = "area.new";
    prompt.title = "New Area";
    prompt.message = "Create a blank rectangular area. Width and height must be between 2 and 32 tiles.";
    prompt.fields = {
        {"ResRef", {}, {}, false},
        {"Directory",
            default_directory.lexically_relative(current_project_dir_)
                .generic_string(),
            {}, true},
        {"Name", {}, {}, false},
        {"Tileset", selected_tileset, std::move(choices), false},
        {"Width", "4", {}, false},
        {"Height", "4", {}, false},
    };
    prompt.actions = {
        {"create", "Create", "area.create", {}},
        {"cancel", "Cancel", "area.cancel", {}},
    };
    CommandResult result;
    result.prompt = std::move(prompt);
    return result;
}

CommandResult ToolsetBackend::submit_new_area_form(
    const CommandInvocation& invocation, CommandContext&)
{
    constexpr size_t field_count = 6;
    if (!workspace_ || (invocation.args.size() != field_count && invocation.args.size() != field_count + 2)) {
        return failure("New area form has incomplete inputs");
    }
    std::array<std::string, field_count> values;
    for (size_t index = 0; index < values.size(); ++index) {
        values[index] = command_arg_string(invocation.args, index);
    }
    const auto reject = [&](std::string message) {
        auto result = show_new_area_form();
        if (!result.prompt) { return failure(std::move(message)); }
        for (size_t index = 0; index < values.size(); ++index) {
            result.prompt->fields[index].value = values[index];
        }
        result.status = CommandStatus::rejected;
        result.message = message;
        result.output_channel = CommandOutputChannel::warn;
        result.prompt->detail = std::move(message);
        return result;
    };

    const bool confirmed = invocation.args.size() == field_count + 2;
    const std::string decision = confirmed
        ? command_arg_string(invocation.args, field_count)
        : std::string{};
    const std::string expected_current = confirmed
        ? command_arg_string(invocation.args, field_count + 1)
        : std::string{};
    auto* current = workspace_->find_tab("area");
    if (confirmed) {
        if ((decision != "--save-current-area"
                && decision != "--discard-current-area")
            || !current || !current->dirty
            || current->detail != expected_current) {
            return reject("The current area changed; submit the new area again");
        }
    } else if (current && current->dirty) {
        auto result = failure("Save changes before creating a new area?");
        CommandPrompt prompt;
        prompt.id = "area.new.save";
        prompt.title = "Save changes?";
        prompt.message = "Save changes to " + current->title
            + " before creating the new area?";
        prompt.detail = current->detail;
        std::vector<std::string> save_args(values.begin(), values.end());
        save_args.push_back("--save-current-area");
        save_args.push_back(current->detail);
        auto discard_args = save_args;
        discard_args[field_count] = "--discard-current-area";
        prompt.actions = {
            {"save", "Save", "area.create", std::move(save_args)},
            {"discard", "Discard", "area.create", std::move(discard_args)},
            {"cancel", "Cancel", "area.cancel", {}},
        };
        result.prompt = std::move(prompt);
        return result;
    }

    int32_t width = 0;
    int32_t height = 0;
    if (!parse_dimension(values[4], width)
        || !parse_dimension(values[5], height)) {
        return reject("Area width and height must be whole numbers between 2 and 32");
    }
    if (decision == "--save-current-area") {
        const std::array<std::string_view, 1> ids{"area"};
        auto saved = save_workspace_documents(
            *workspace_, current_project_dir_, ids, module_object_);
        if (!saved.ok()) { return saved; }
    }

    const std::array requests{NewAreaRequest{
        .directory = values[1],
        .resref = values[0],
        .name = values[2],
        .tileset = Resref{values[3]},
        .width = width,
        .height = height,
    }};
    auto prepared = prepare_new_areas(current_project_dir_, requests);
    if (!prepared.ok()) { return reject(prepared.error); }
    const auto publication = publish_new_areas(prepared);
    if (publication.rows.size() != 1 || !publication.rows[0].published) {
        return reject(publication.rows.empty()
                ? "Area publication failed"
                : publication.rows[0].error);
    }
    if (decision == "--discard-current-area" && current) {
        current->dirty = false;
    }
    refresh_loaded_project_areas();
    const std::string resource
        = publication.rows[0].relative_path.generic_string();
    workspace_->open_area_tab(resource, prepared.rows[0].request.name);
    std::string message = "Created area: " + resource;
    CommandOutputChannel channel = CommandOutputChannel::info;
    if (publication.maps.failed > 0) {
        message += "; area map unavailable";
        if (!publication.maps.first_error.empty()) {
            message += ": " + publication.maps.first_error;
        }
        channel = CommandOutputChannel::warn;
    } else if (publication.maps.degraded > 0) {
        message += "; area map contains missing-tile markers";
        if (!publication.maps.first_warning.empty()) {
            message += ": " + publication.maps.first_warning;
        }
        channel = CommandOutputChannel::warn;
    }
    return {CommandStatus::success, std::move(message), channel};
}

CommandResult ToolsetBackend::delete_current_area(
    const CommandInvocation& invocation)
{
    if (!workspace_) { return failure("Workspace unavailable"); }
    auto* current = workspace_->active_tab();
    if (!current || current->id != "area"
        || current->kind != WorkspaceTabKind::area
        || current->detail.empty()) {
        return failure("Open and activate a project Area before deleting it");
    }

    AreaDeleteTarget target;
    std::string error;
    if (!resolve_area_delete_target(
            current_project_dir_, current->detail, target, error)) {
        return failure(std::move(error));
    }

    if (invocation.args.empty()) {
        auto result = failure("Confirm permanent Area deletion");
        CommandPrompt prompt;
        prompt.id = "area.delete";
        prompt.title = "Delete Area?";
        prompt.message = "Permanently delete " + current->title + "?";
        prompt.detail = target.relative.generic_string();
        if (current->dirty) {
            prompt.detail += "\nUnsaved changes and undo history will be discarded.";
        }
        prompt.actions = {
            {"delete", "Delete", "area.delete",
                {"--confirm", current->detail}},
            {"cancel", "Cancel", {}, {}},
        };
        result.prompt = std::move(prompt);
        return result;
    }

    if (invocation.args.size() != 2
        || command_arg_string(invocation.args, 0) != "--confirm"
        || command_arg_string(invocation.args, 1) != current->detail) {
        return failure(
            "The active Area changed; request deletion again");
    }

    std::string bytes;
    if (!read_file(target.target, bytes)) {
        return failure("Failed to read Area before deletion: "
            + target.target.string());
    }

    std::error_code ec;
    if (!std::filesystem::remove(target.target, ec) || ec) {
        return failure("Failed to delete Area: " + target.target.string()
            + (ec ? ": " + ec.message() : std::string{}));
    }

    String refresh_error;
    if (!kernel::resman().refresh_module_resources(refresh_error)) {
        const std::array writes{ResourceFileWrite{
            target.target, bytes, ResourceFileWriteMode::create, {}}};
        const auto restored = write_resource_files_atomic(writes);
        String rollback_refresh_error;
        const bool rollback_refreshed = !restored.empty()
            && restored[0].written
            && kernel::resman().refresh_module_resources(
                rollback_refresh_error);
        std::string message
            = "Area deletion failed during resource refresh: "
            + std::string{refresh_error};
        if (rollback_refreshed) {
            message += "; original Area restored";
        } else if (!restored.empty() && restored[0].written) {
            message += "; original Area file restored, but resource refresh failed";
            if (!rollback_refresh_error.empty()) {
                message += ": " + std::string{rollback_refresh_error};
            }
        } else {
            message += "; failed to restore original Area";
            if (!restored.empty() && !restored[0].error.empty()) {
                message += ": " + restored[0].error;
            }
        }
        return {CommandStatus::failed, std::move(message),
            CommandOutputChannel::error};
    }

    ec.clear();
    bool map_removed = true;
    if (std::filesystem::exists(target.map, ec)) {
        map_removed = std::filesystem::remove(target.map, ec);
    }
    if (ec) { map_removed = false; }

    current->dirty = false;
    workspace_->open_area_tab({}, "Area");
    workspace_->set_active_tab("home");
    if (bridge_) {
        bridge_->clear_active_object();
        bridge_->clear_active_area();
    }
    refresh_loaded_project_areas();

    CommandResult result;
    result.message = "Deleted area: " + target.relative.generic_string();
    result.refreshed_area_maps.push_back(target.map);
    if (!map_removed || ec) {
        result.message += "; stale derived map could not be removed";
        if (ec) { result.message += ": " + ec.message(); }
        result.output_channel = CommandOutputChannel::warn;
    }
    return result;
}

} // namespace nw::toolset
