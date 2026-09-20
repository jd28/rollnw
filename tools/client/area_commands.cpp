#include "toolset_backend.hpp"

#include "area_creation.hpp"
#include "object_document.hpp"

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
        }
        if (label.empty()) { label = tileset.name; }
        if (label.empty()) { label = resref; }
        if (label != resref) { label += " (" + resref + ")"; }
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
            *workspace_, current_project_dir_, ids);
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
    const auto published = publish_new_areas(prepared);
    if (published.size() != 1 || !published[0].published) {
        return reject(published.empty()
                ? "Area publication failed"
                : published[0].error);
    }
    if (decision == "--discard-current-area" && current) {
        current->dirty = false;
    }
    refresh_loaded_project_areas();
    const std::string resource = published[0].relative_path.generic_string();
    workspace_->open_area_tab(resource, prepared.rows[0].request.name);
    return {CommandStatus::success,
        "Created area: " + resource, CommandOutputChannel::info};
}

} // namespace nw::toolset
