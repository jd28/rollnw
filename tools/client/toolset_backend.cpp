#include "toolset_backend.hpp"

#include "area_map.hpp"
#include "forward_plus_debug.hpp"
#include "object_edits.hpp"
#include "script_commands.hpp"
#include "ui_v1.hpp"

#include "smalls_creature_properties.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/viewer/area_lighting.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/string.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <unordered_set>
#include <utility>

namespace nw::toolset {

namespace {

std::string to_lower_ascii(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool contains_casefold(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }
    const std::string hay = to_lower_ascii(haystack);
    const std::string ndl = to_lower_ascii(needle);
    return hay.find(ndl) != std::string::npos;
}

bool is_blank_ascii(std::string_view value)
{
    for (const unsigned char ch : value) {
        if (!std::isspace(ch)) {
            return false;
        }
    }
    return true;
}

std::optional<uint32_t> parse_positive_u32(std::string_view value)
{
    std::string text{value};
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed == 0
        || parsed > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> parse_u32(std::string_view value)
{
    std::string text{value};
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE
        || parsed > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

std::optional<int32_t> parse_i32(std::string_view value)
{
    int32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<UiListSelection> parse_list_selection(
    const CommandInvocation& invocation)
{
    const std::string list_id = command_arg_string(invocation.args, 0);
    const std::string key = command_arg_string(invocation.args, 1);
    const auto index = parse_i32(command_arg_string(invocation.args, 2));
    if (list_id.empty() || key.empty() || !index) {
        return std::nullopt;
    }
    return UiListSelection{
        .list_id = list_id,
        .key = key,
        .index = *index,
        .cell = -1,
    };
}

std::optional<bool> parse_assignment(std::string_view value)
{
    if (value == "1" || value == "true" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<float> parse_finite_f32(std::string_view value)
{
    const auto parsed = nw::string::from<float>(value);
    if (!parsed || !std::isfinite(*parsed)) {
        return std::nullopt;
    }
    return *parsed;
}

std::optional<ObjectVariableType> parse_object_variable_type(std::string_view value)
{
    const std::string normalized = to_lower_ascii(value);
    if (normalized == "1" || normalized == "integer" || normalized == "int") {
        return ObjectVariableType::integer;
    }
    if (normalized == "2" || normalized == "float" || normalized == "floating") {
        return ObjectVariableType::floating;
    }
    if (normalized == "3" || normalized == "string") {
        return ObjectVariableType::string;
    }
    return std::nullopt;
}

std::optional<ObjectVariableRecord> find_object_variable(
    ObjectHandle object,
    std::string_view name,
    ObjectVariableType type,
    std::string& diagnostic)
{
    ObjectVariableSnapshot snapshot;
    snapshot_object_variables(object, snapshot);
    if (snapshot.status != ObjectVariableSnapshotStatus::ready) {
        diagnostic = snapshot.diagnostic.empty()
            ? "Object variable data is unavailable"
            : std::move(snapshot.diagnostic);
        return std::nullopt;
    }
    const auto found = std::find_if(snapshot.rows.begin(), snapshot.rows.end(),
        [&](const ObjectVariableSnapshotRow& row) {
            return row.variable.name == name && row.variable.type == type;
        });
    if (found == snapshot.rows.end()) {
        diagnostic = "Object variable row is stale or missing";
        return std::nullopt;
    }
    return found->variable;
}

ObjectVariableRecord default_object_variable_record(
    std::string name, ObjectVariableType type)
{
    ObjectVariableRecord record;
    record.name = std::move(name);
    record.type = type;
    return record;
}

struct ActiveTransform {
    ObjectHandle object{};
    ObjectTransformState state;
};

std::optional<ActiveTransform> active_transform(const RmlSmallsBridge* bridge)
{
    if (!bridge) {
        return std::nullopt;
    }
    const ObjectHandle object = bridge->active_object();
    if (object.type != ObjectType::creature && object.type != ObjectType::placeable) {
        return std::nullopt;
    }
    const auto* spatial = kernel::objects().components().find_spatial(object);
    if (!spatial) {
        return std::nullopt;
    }
    return ActiveTransform{
        .object = object,
        .state = {
            .position = spatial->position,
            .orientation = spatial->orientation,
            .scale = spatial->scale,
        },
    };
}

glm::vec3 orientation_from_angle(float angle) noexcept
{
    return {std::cos(angle), std::sin(angle), 0.0f};
}

float orientation_angle(glm::vec3 orientation) noexcept
{
    return std::abs(orientation.x) > 1.0e-6f || std::abs(orientation.y) > 1.0e-6f
        ? std::atan2(orientation.y, orientation.x)
        : 0.0f;
}

float random_orientation_angle()
{
    thread_local std::mt19937 generator{std::random_device{}()};
    thread_local std::uniform_real_distribution<float> distribution{
        0.0f, 2.0f * std::numbers::pi_v<float>};
    return distribution(generator);
}

std::optional<bool> creature_has_feat(smalls::Runtime& runtime, ObjectHandle object, uint32_t feat_id)
{
    if (object.type != ObjectType::creature || feat_id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
    }

    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    Vector<smalls::Value> args;
    args.push_back(object_value);
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(feat_id)));
    const auto result = runtime.execute_script("nwn1.creature_state", "has_feat", args);
    if (!result.ok() || result.value.type_id != runtime.bool_type()) {
        return std::nullopt;
    }
    return result.value.data.bval;
}

std::string_view display_name_or_resref(const LoadedAreaEntry& area)
{
    return is_blank_ascii(area.name) ? std::string_view(area.resref) : std::string_view(area.name);
}

void append_project_areas(const ProjectTreeNode& node,
    const std::filesystem::path& project_dir,
    std::vector<LoadedAreaEntry>& areas)
{
    if (node.kind == ProjectTreeNodeKind::area) {
        const nw::Resource resource = nw::Resource::from_path(node.relative_path, false);
        if (resource.valid()) {
            const auto map_path = project_area_map_path(project_dir, resource.resref.view());
            std::error_code ec;
            areas.push_back(LoadedAreaEntry{
                .name = node.label,
                .resref = std::string{resource.resref.view()},
                .resource = node.relative_path.generic_string(),
                .map_path = std::filesystem::is_regular_file(map_path, ec)
                    ? map_path
                    : std::filesystem::path{},
            });
        }
        return;
    }

    for (const auto& child : node.children) {
        append_project_areas(child, project_dir, areas);
    }
}

std::string sanitize_label(std::string_view text)
{
    std::string out;
    out.reserve(text.size());

    for (const unsigned char ch : text) {
        if (ch == 0 || ch < 0x20 || ch == 0x7f) {
            continue;
        }
        out.push_back(static_cast<char>(ch));
    }

    size_t first = 0;
    while (first < out.size() && std::isspace(static_cast<unsigned char>(out[first]))) {
        ++first;
    }
    size_t last = out.size();
    while (last > first && std::isspace(static_cast<unsigned char>(out[last - 1]))) {
        --last;
    }

    return out.substr(first, last - first);
}

std::string pick_locstring_name(const ::nw::LocString& loc)
{
    std::string value = sanitize_label(loc.get(::nw::LanguageID::english));
    if (!is_blank_ascii(value)) {
        return value;
    }

    for (const auto& [lang, localized] : loc) {
        (void)lang;
        const std::string cleaned = sanitize_label(localized);
        if (!is_blank_ascii(cleaned)) {
            return cleaned;
        }
    }
    return {};
}

bool is_module_container(const std::filesystem::directory_entry& entry)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (entry.is_directory(ec)) {
        return fs::exists(entry.path() / "module.ifo", ec)
            || fs::exists(entry.path() / "module.ifo.json", ec)
            || fs::exists(entry.path() / "shared" / "module.ifo", ec)
            || fs::exists(entry.path() / "shared" / "module.ifo.json", ec);
    }

    if (!entry.is_regular_file(ec)) {
        return false;
    }

    const auto ext = entry.path().extension().string();
    return ext == ".mod" || ext == ".zip";
}

bool relative_path_escapes_root(const std::filesystem::path& relative)
{
    for (const auto& part : relative) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

CommandResult command_result(CommandStatus status, std::string message, CommandOutputChannel channel = CommandOutputChannel::info)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = channel;
    return result;
}

CommandContext context_with_backend_defaults(CommandContext context, WorkspaceState* workspace)
{
    context.workspace = workspace;
    if (context.active_tab_id.empty() && workspace) {
        context.active_tab_id = workspace->active_tab_id();
    }
    return context;
}

} // namespace

void ToolsetBackend::bind(RmlSmallsBridge* bridge, ShellController* shell, WorkspaceState* workspace) noexcept
{
    bridge_ = bridge;
    shell_ = shell;
    workspace_ = workspace;
    script_command_host().bind(&command_bus_, workspace_);
    register_native_commands();
}

void ToolsetBackend::set_document_save_handler(DocumentSaveHandler handler)
{
    document_save_handler_ = std::move(handler);
}

bool ToolsetBackend::initialize()
{
    if (!bridge_ || !bridge_->initialize()) {
        return false;
    }
    return ensure_data_object_editor_lists();
}

bool ToolsetBackend::initialize_item_editor_data_model(Rml::Context& context)
{
    const bool initialized = item_editor_data_model_.initialize(context,
        [this](std::string_view command,
            std::span<const int32_t> arguments,
            std::string& diagnostic) {
            std::vector<std::string> storage;
            storage.reserve(arguments.size());
            for (const int32_t value : arguments) {
                storage.push_back(std::to_string(value));
            }
            std::vector<std::string_view> views;
            views.reserve(storage.size());
            for (const auto& value : storage) {
                views.push_back(value);
            }

            CommandContext command_context{
                .source = CommandSource::widget,
                .workspace = workspace_,
            };
            auto result = execute_command(
                command, views, std::move(command_context));
            if (result.should_log() && shell_) {
                shell_->append_output(
                    command_output_channel_name(result.output_channel),
                    result.message);
            }
            if (!result.ok()) {
                diagnostic = result.message;
            }
            return result.ok();
        });
    if (initialized) {
        item_editor_data_model_.refresh(item_editor_.appearance_input());
    }
    return initialized;
}

bool ToolsetBackend::apply_item_editor_pending_focus(
    Rml::ElementDocument* document)
{
    return item_editor_data_model_.apply_pending_focus(document);
}

void ToolsetBackend::shutdown_item_editor_data_model()
{
    item_editor_data_model_.shutdown();
}

bool ToolsetBackend::refresh_creature_body_part_editor()
{
    auto& host = ui_v1_host();
    if (!bridge_) {
        return creature_body_part_editor_.clear(host);
    }
    const ObjectHandle object = bridge_->active_object();
    const auto snapshot = creature_body_part_editor_snapshot(
        kernel::runtime(), object);
    if (!snapshot || snapshot->assembly < 0) {
        return creature_body_part_editor_.clear(host);
    }
    return creature_body_part_editor_.refresh(
        CreatureBodyPartEditorInput{
            .object = object,
            .assembly = snapshot->assembly,
            .values = snapshot->values,
        },
        kernel::rules().creature_body_parts,
        host);
}

bool ToolsetBackend::refresh_item_editor()
{
    const ObjectHandle object = bridge_ ? bridge_->active_object() : ObjectHandle{};
    if (!item_editor_.refresh(kernel::runtime(), object, ui_v1_host())) {
        return false;
    }
    item_editor_data_model_.refresh(item_editor_.appearance_input());
    return true;
}

bool ToolsetBackend::ensure_data_object_editor_lists()
{
    auto& host = ui_v1_host();
    if (data_object_list_generation_ == host.generation()) {
        return true;
    }

    static constexpr std::array<std::string_view, 3> list_ids{
        "data.encounter.spawns",
        "data.sound.resources",
        "data.store.inventory",
    };
    static constexpr UiListConfig config{
        .row_height = 34,
        .overscan = 6,
        .columns = 1,
    };
    for (const auto list_id : list_ids) {
        if (!host.contains(list_id)
            && !host.create(std::string{list_id}, config)) {
            return false;
        }
        if (!host.set_visible(list_id, true)) {
            return false;
        }
    }

    data_object_list_generation_ = host.generation();
    return true;
}

bool ToolsetBackend::creature_body_part_editor_is_current() const noexcept
{
    return bridge_
        && creature_body_part_editor_.object().type == ObjectType::creature
        && bridge_->active_object() == creature_body_part_editor_.object();
}

bool ToolsetBackend::item_editor_is_current() const noexcept
{
    return bridge_ && item_editor_.object().type == ObjectType::item
        && bridge_->active_object() == item_editor_.object();
}

void ToolsetBackend::register_native_commands()
{
    if (command_bus_.has_command("command.undo")) {
        return;
    }

    auto register_or_log = [this](CommandSpec spec, CommandBus::Handler handler) {
        std::string error;
        if (!command_bus_.register_command(std::move(spec), std::move(handler), &error) && shell_) {
            shell_->append_output("warn", error);
        }
    };

    auto register_area_toggle = [&](CommandSpec spec,
                                    bool ShellController::* field,
                                    std::string enabled_message,
                                    std::string disabled_message) {
        register_or_log(std::move(spec),
            [this, field, enabled_message = std::move(enabled_message), disabled_message = std::move(disabled_message)](
                const CommandInvocation&, CommandContext&) {
                if (!shell_) {
                    return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
                }
                (shell_->*field) = !(shell_->*field);
                return command_result(CommandStatus::success,
                    (shell_->*field) ? enabled_message : disabled_message,
                    CommandOutputChannel::info);
            });
    };

    const auto register_hidden_editor_command = [&register_or_log](
                                                    std::string id,
                                                    CommandBus::Handler handler) {
        CommandSpec spec;
        spec.id = std::move(id);
        spec.title = spec.id;
        spec.category = "editor";
        spec.scope = CommandScope::global;
        spec.flags = CommandFlags::hidden;
        register_or_log(std::move(spec), std::move(handler));
    };

    register_hidden_editor_command(
        "toolset.data_objects.initialize",
        [this](const CommandInvocation&, CommandContext&) {
            return command_result(
                ensure_data_object_editor_lists()
                    ? CommandStatus::success
                    : CommandStatus::failed,
                {}, CommandOutputChannel::none);
        });
    register_hidden_editor_command(
        "toolset.creature.body_parts.refresh",
        [this](const CommandInvocation&, CommandContext&) {
            return command_result(
                refresh_creature_body_part_editor()
                    ? CommandStatus::success
                    : CommandStatus::failed,
                {}, CommandOutputChannel::none);
        });
    register_hidden_editor_command(
        "toolset.creature.body_parts.close",
        [this](const CommandInvocation&, CommandContext&) {
            return command_result(
                creature_body_part_editor_.close_options(ui_v1_host())
                    ? CommandStatus::success
                    : CommandStatus::failed,
                {}, CommandOutputChannel::none);
        });
    register_hidden_editor_command(
        "toolset.creature.body_parts.activate",
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!creature_body_part_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Creature body-part editor",
                    CommandOutputChannel::warn);
            }
            auto selection = parse_list_selection(invocation);
            if (!selection) {
                return command_result(CommandStatus::rejected,
                    "Invalid Creature body-part selection",
                    CommandOutputChannel::warn);
            }
            const bool activated = creature_body_part_editor_.activate_part(
                *selection, kernel::rules().creature_body_parts, ui_v1_host());
            return command_result(
                activated ? CommandStatus::success : CommandStatus::rejected,
                activated ? std::string{}
                          : "Stale Creature body-part selection",
                activated ? CommandOutputChannel::none
                          : CommandOutputChannel::warn);
        });
    register_hidden_editor_command(
        "toolset.creature.body_parts.option.activate",
        [this](const CommandInvocation& invocation, CommandContext& context) {
            if (!creature_body_part_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Creature body-part editor",
                    CommandOutputChannel::warn);
            }
            const auto selection = parse_list_selection(invocation);
            const auto edit = selection
                ? creature_body_part_editor_.activate_option(*selection)
                : std::nullopt;
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Stale Creature body-part option selection",
                    CommandOutputChannel::warn);
            }

            const std::array<std::string, 2> storage{
                std::to_string(edit->part), std::to_string(edit->value)};
            const std::vector<std::string_view> args{storage[0], storage[1]};
            auto result = execute_command(
                "object.creature.set_body_part", args, context);
            if (result.ok()
                && (!creature_body_part_editor_.hide_options(ui_v1_host())
                    || !refresh_creature_body_part_editor())) {
                return command_result(CommandStatus::failed,
                    "Creature body-part editor refresh failed",
                    CommandOutputChannel::error);
            }
            return result;
        });

    const auto item_result = [](bool ok) {
        return command_result(
            ok ? CommandStatus::success : CommandStatus::failed,
            ok ? std::string{} : std::string{"Item editor operation failed"},
            ok ? CommandOutputChannel::none : CommandOutputChannel::error);
    };
    const auto item_state_result = [this, item_result](bool ok) {
        if (ok) {
            item_editor_data_model_.refresh(item_editor_.appearance_input());
        }
        return item_result(ok);
    };
    const auto stale_item_editor_result = []() {
        return command_result(CommandStatus::rejected,
            "Stale Item editor", CommandOutputChannel::warn);
    };
    register_hidden_editor_command(
        "toolset.item.initialize",
        [this, item_result](const CommandInvocation&, CommandContext&) {
            return item_result(refresh_item_editor());
        });
    register_hidden_editor_command(
        "toolset.item.refresh",
        [this, item_result](const CommandInvocation&, CommandContext&) {
            return item_result(refresh_item_editor());
        });
    register_hidden_editor_command(
        "toolset.item.details",
        [this](const CommandInvocation&, CommandContext&) {
            if (!refresh_item_editor()) {
                return command_result(CommandStatus::failed,
                    "Item editor refresh failed", CommandOutputChannel::error);
            }
            return command_result(CommandStatus::success,
                item_editor_.has_inventory() ? "1" : "0",
                CommandOutputChannel::none);
        });
    register_hidden_editor_command(
        "toolset.item.appearance.open_model",
        [this, item_state_result, stale_item_editor_result](
            const CommandInvocation& invocation, CommandContext&) {
            if (!item_editor_is_current()) {
                return stale_item_editor_result();
            }
            const auto part = parse_i32(command_arg_string(invocation.args, 0));
            const auto axis = parse_i32(command_arg_string(invocation.args, 1));
            return item_state_result(part && axis
                && item_editor_.open_model(
                    kernel::runtime(), *part, *axis, ui_v1_host()));
        });
    register_hidden_editor_command(
        "toolset.item.appearance.close",
        [this, item_state_result](const CommandInvocation&, CommandContext&) {
            return item_state_result(
                item_editor_.close_appearance(ui_v1_host()));
        });
    register_hidden_editor_command(
        "toolset.item.appearance.open_color",
        [this, item_state_result, stale_item_editor_result](
            const CommandInvocation& invocation, CommandContext&) {
            if (!item_editor_is_current()) {
                return stale_item_editor_result();
            }
            const auto part = parse_i32(command_arg_string(invocation.args, 0));
            const auto color = parse_i32(command_arg_string(invocation.args, 1));
            return item_state_result(part && color
                && item_editor_.open_color(*part, *color, ui_v1_host()));
        });
    register_hidden_editor_command(
        "toolset.item.appearance.select_color",
        [this, item_state_result, stale_item_editor_result](
            const CommandInvocation& invocation, CommandContext&) {
            if (!item_editor_is_current()) {
                return stale_item_editor_result();
            }
            const auto color = parse_i32(command_arg_string(invocation.args, 0));
            return item_state_result(
                color && item_editor_.select_color(*color));
        });

    const auto apply_item_color = [this](int32_t value,
                                      CommandContext& context) {
        if (!item_editor_is_current()) {
            return command_result(CommandStatus::rejected,
                "Stale Item editor", CommandOutputChannel::warn);
        }
        const auto edit = item_editor_.color_edit(value);
        if (!edit) {
            return command_result(CommandStatus::rejected,
                "Invalid Item color selection", CommandOutputChannel::warn);
        }
        const std::array<std::string, 3> storage{
            std::to_string(edit->part), std::to_string(edit->color),
            std::to_string(edit->value)};
        const std::vector<std::string_view> args{
            storage[0], storage[1], storage[2]};
        auto result = execute_command("object.item.set_color", args, context);
        if (result.ok()) {
            if (!refresh_item_editor()) {
                return command_result(CommandStatus::failed,
                    "Item editor refresh failed", CommandOutputChannel::error);
            }
            result.message.clear();
            result.output_channel = CommandOutputChannel::none;
        }
        return result;
    };
    register_hidden_editor_command(
        "toolset.item.appearance.apply_color",
        [apply_item_color](
            const CommandInvocation& invocation, CommandContext& context) {
            const auto value = parse_i32(command_arg_string(invocation.args, 0));
            return value ? apply_item_color(*value, context)
                         : command_result(CommandStatus::rejected,
                               "Invalid Item color value",
                               CommandOutputChannel::warn);
        });
    register_hidden_editor_command(
        "toolset.item.appearance.inherit_color",
        [apply_item_color](const CommandInvocation&, CommandContext& context) {
            return apply_item_color(255, context);
        });
    register_hidden_editor_command(
        "toolset.item.appearance.model.activate",
        [this](const CommandInvocation& invocation, CommandContext& context) {
            if (!item_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Item editor", CommandOutputChannel::warn);
            }
            const auto selection = parse_list_selection(invocation);
            const auto edit = selection
                ? item_editor_.activate_model(*selection)
                : std::nullopt;
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Stale Item model selection", CommandOutputChannel::warn);
            }
            const std::array<std::string, 2> storage{
                std::to_string(edit->part), std::to_string(edit->value)};
            const std::vector<std::string_view> args{storage[0], storage[1]};
            auto result = execute_command(
                "object.item.set_model_part", args, context);
            if (result.ok()) {
                if (!item_editor_.hide_model_options(ui_v1_host())
                    || !refresh_item_editor()) {
                    return command_result(CommandStatus::failed,
                        "Item editor refresh failed",
                        CommandOutputChannel::error);
                }
                item_editor_data_model_.request_model_focus();
                result.message.clear();
                result.output_channel = CommandOutputChannel::none;
            }
            return result;
        });
    register_hidden_editor_command(
        "toolset.item.properties.add",
        [this](const CommandInvocation&, CommandContext& context) {
            if (!item_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Item editor", CommandOutputChannel::warn);
            }
            const auto source = item_editor_.selected_available_property(
                ui_v1_host());
            if (!source) {
                return command_result(CommandStatus::rejected,
                    "No available Item property is selected",
                    CommandOutputChannel::warn);
            }
            const int32_t inserted = static_cast<int32_t>(
                item_editor_.applied_property_count());
            const std::array<std::string, 6> storage{
                std::to_string(source->prop_type),
                std::to_string(source->subtype),
                std::to_string(source->cost_table),
                std::to_string(source->cost_value),
                std::to_string(source->param_table),
                std::to_string(source->param_value)};
            const std::vector<std::string_view> args{storage.begin(), storage.end()};
            auto result = execute_command(
                "object.item.add_property", args, context);
            if (result.ok()
                && (!refresh_item_editor()
                    || !item_editor_.select_applied(inserted, ui_v1_host()))) {
                return command_result(CommandStatus::failed,
                    "Item property editor refresh failed",
                    CommandOutputChannel::error);
            }
            return result;
        });
    register_hidden_editor_command(
        "toolset.item.properties.remove",
        [this](const CommandInvocation&, CommandContext& context) {
            if (!item_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Item editor", CommandOutputChannel::warn);
            }
            const auto index = item_editor_.selected_applied_property(
                ui_v1_host());
            if (!index) {
                return command_result(CommandStatus::rejected,
                    "No applied Item property is selected",
                    CommandOutputChannel::warn);
            }
            const std::string storage = std::to_string(*index);
            auto result = execute_command(
                "object.item.remove_property", {storage}, context);
            if (result.ok()) {
                if (!refresh_item_editor()) {
                    return command_result(CommandStatus::failed,
                        "Item property editor refresh failed",
                        CommandOutputChannel::error);
                }
                const int32_t count = static_cast<int32_t>(
                    item_editor_.applied_property_count());
                const int32_t selected = count == 0
                    ? -1
                    : std::min(*index, count - 1);
                if (!item_editor_.select_applied(selected, ui_v1_host())) {
                    return command_result(CommandStatus::failed,
                        "Item property selection refresh failed",
                        CommandOutputChannel::error);
                }
            }
            return result;
        });
    register_hidden_editor_command(
        "toolset.item.properties.close",
        [this](const CommandInvocation&, CommandContext&) {
            return command_result(
                item_editor_.close_property_options(ui_v1_host())
                    ? CommandStatus::success
                    : CommandStatus::failed,
                {}, CommandOutputChannel::none);
        });
    register_hidden_editor_command(
        "toolset.item.properties.applied.activate",
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!item_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Item editor", CommandOutputChannel::warn);
            }
            auto selection = parse_list_selection(invocation);
            if (!selection) {
                return command_result(CommandStatus::rejected,
                    "Invalid Item property selection",
                    CommandOutputChannel::warn);
            }
            selection->cell = parse_i32(
                command_arg_string(invocation.args, 3))
                                  .value_or(-1);
            if (selection->cell == -1 || selection->cell == 0) {
                return command_result(CommandStatus::success, {},
                    CommandOutputChannel::none);
            }
            const bool opened = item_editor_.open_property_options(
                kernel::runtime(), *selection, ui_v1_host());
            return command_result(
                opened ? CommandStatus::success : CommandStatus::rejected,
                opened ? std::string{} : "Stale Item property selection",
                opened ? CommandOutputChannel::none
                       : CommandOutputChannel::warn);
        });
    register_hidden_editor_command(
        "toolset.item.properties.option.activate",
        [this](const CommandInvocation& invocation, CommandContext& context) {
            if (!item_editor_is_current()) {
                return command_result(CommandStatus::rejected,
                    "Stale Item editor", CommandOutputChannel::warn);
            }
            const auto selection = parse_list_selection(invocation);
            const auto edit = selection
                ? item_editor_.activate_property_option(*selection)
                : std::nullopt;
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Stale Item property option selection",
                    CommandOutputChannel::warn);
            }
            static constexpr std::array<std::string_view, 3> fields{
                "subtype", "param", "cost"};
            const std::array<std::string, 3> storage{
                std::to_string(edit->index),
                std::string{fields[static_cast<size_t>(edit->field)]},
                std::to_string(edit->value)};
            const std::vector<std::string_view> args{
                storage[0], storage[1], storage[2]};
            auto result = execute_command(
                "object.item.set_property_value", args, context);
            if (result.ok() && !refresh_item_editor()) {
                return command_result(CommandStatus::failed,
                    "Item property editor refresh failed",
                    CommandOutputChannel::error);
            }
            return result;
        });

    register_or_log(CommandSpec{
                        "command.undo",
                        "Undo",
                        "Undo the last edit in the active workspace tab",
                        "workspace",
                        {"undo"},
                        CommandScope::workspace,
                        CommandFlags::none,
                        "Ctrl+Z",
                        "command.undo",
                    },
        [this](const CommandInvocation&, CommandContext context) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            return workspace_->undo(context);
        });

    register_or_log(CommandSpec{
                        "command.redo",
                        "Redo",
                        "Redo the last undone edit in the active workspace tab",
                        "workspace",
                        {"redo"},
                        CommandScope::workspace,
                        CommandFlags::none,
                        "Ctrl+Y",
                        "command.redo",
                    },
        [this](const CommandInvocation&, CommandContext context) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            return workspace_->redo(context);
        });

    register_or_log(CommandSpec{
                        "smalls.call",
                        "Call Smalls Function",
                        "Call a named function in a loaded Smalls module",
                        "scripting",
                        {"smalls"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "smalls.call <module> <function> [string-args...]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!bridge_ || !bridge_->initialized()) {
                return command_result(CommandStatus::failed, "Smalls runtime unavailable", CommandOutputChannel::error);
            }
            if (invocation.args.size() < 2) {
                return command_result(CommandStatus::rejected,
                    "Usage: smalls.call <module> <function> [string-args...]",
                    CommandOutputChannel::warn);
            }

            const std::string module_path = command_arg_string(invocation.args, 0);
            const std::string function_name = command_arg_string(invocation.args, 1);
            std::vector<std::string> argument_storage;
            argument_storage.reserve(invocation.args.size() - 2);
            for (size_t i = 2; i < invocation.args.size(); ++i) {
                argument_storage.push_back(command_arg_string(invocation.args, i));
            }
            std::vector<std::string_view> arguments;
            arguments.reserve(argument_storage.size());
            for (const auto& argument : argument_storage) {
                arguments.push_back(argument);
            }

            const auto result = bridge_->invoke(module_path, function_name, arguments);
            return command_result(result.ok ? CommandStatus::success : CommandStatus::failed,
                result.message,
                result.ok ? CommandOutputChannel::script : CommandOutputChannel::error);
        });

    register_or_log(CommandSpec{
                        "workspace.open_tab",
                        "Open Workspace Tab",
                        "Open a workspace tab and make it active",
                        "workspace",
                        {"tab.open"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "workspace.open_tab <id> [title]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const std::string id = command_arg_string(invocation.args, 0);
            if (id.empty()) {
                return command_result(CommandStatus::rejected, "Workspace tab id required", CommandOutputChannel::warn);
            }
            std::string title = command_arg_string(invocation.args, 1);
            workspace_->open_tab(id, title);
            return command_result(CommandStatus::success, std::string{"Opened tab: "} + id);
        });

    register_or_log(CommandSpec{
                        "workspace.home",
                        "Home Tab",
                        "Open the workspace home tab",
                        "workspace",
                        {"home", "tab.home"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "workspace.home",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const std::string title = current_project_dir_.empty()
                ? std::string{"Home"}
                : project_display_name(current_project_dir_);
            workspace_->ensure_default_tabs(title);
            return command_result(CommandStatus::success, "Opened Home", CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "workspace.activate_tab",
                        "Activate Workspace Tab",
                        "Make an existing workspace tab active",
                        "workspace",
                        {"tab.activate"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "workspace.activate_tab <id>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const std::string id = command_arg_string(invocation.args, 0);
            if (id.empty()) {
                return command_result(CommandStatus::rejected, "Workspace tab id required", CommandOutputChannel::warn);
            }
            if (!workspace_->set_active_tab(id)) {
                return command_result(CommandStatus::rejected, std::string{"Unknown workspace tab: "} + id, CommandOutputChannel::warn);
            }
            return command_result(CommandStatus::success, std::string{"Active tab: "} + id, CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "workspace.close_tab",
                        "Close Workspace Tab",
                        "Close a workspace tab",
                        "workspace",
                        {"tab.close"},
                        CommandScope::global,
                        CommandFlags::none,
                        "Ctrl+W",
                        "workspace.close_tab [id]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            std::string id;
            bool force = false;
            for (size_t i = 0; i < invocation.args.size(); ++i) {
                const std::string value = command_arg_string(invocation.args, i);
                if (value == "--force" || value == "force") {
                    force = true;
                } else if (id.empty()) {
                    id = value;
                }
            }
            if (id.empty()) {
                id = workspace_->active_tab_id();
            }
            if (id.empty()) {
                return command_result(CommandStatus::noop, "No workspace tab closed", CommandOutputChannel::info);
            }
            const auto close = workspace_->request_close_tab(id, force);
            if (close.needs_save_prompt()) {
                auto result = command_result(CommandStatus::rejected,
                    std::string{"Save changes before closing: "} + (close.title.empty() ? close.tab_id : close.title),
                    CommandOutputChannel::warn);
                CommandPrompt prompt;
                prompt.id = "workspace.close_tab.save";
                prompt.title = "Save changes?";
                prompt.message = "Save changes before closing " + (close.title.empty() ? close.tab_id : close.title) + "?";
                prompt.detail = close.detail;
                prompt.actions.push_back(CommandPromptAction{
                    "save",
                    "Save",
                    "workspace.save_and_close_tab",
                    {close.tab_id},
                });
                prompt.actions.push_back(CommandPromptAction{
                    "discard",
                    "Discard",
                    "workspace.close_tab",
                    {close.tab_id, "--force"},
                });
                prompt.actions.push_back(CommandPromptAction{
                    "cancel",
                    "Cancel",
                    {},
                    {},
                });
                result.prompt = std::move(prompt);
                return result;
            }
            if (!close.closed()) {
                return command_result(CommandStatus::noop, "No workspace tab closed", CommandOutputChannel::info);
            }
            return command_result(CommandStatus::success, std::string{"Closed tab: "} + id);
        });

    register_or_log(CommandSpec{
                        "workspace.save_tab",
                        "Save Workspace Tab",
                        "Save a dirty workspace tab",
                        "workspace",
                        {"tab.save"},
                        CommandScope::workspace,
                        CommandFlags::none,
                        "Ctrl+S",
                        "workspace.save_tab [id]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const std::string id = [&] {
                const std::string argument = command_arg_string(invocation.args, 0);
                return argument.empty() ? workspace_->active_tab_id() : argument;
            }();
            if (id.empty()) {
                return command_result(CommandStatus::noop, "No workspace tab to save", CommandOutputChannel::info);
            }
            if (!document_save_handler_) {
                return command_result(CommandStatus::failed, "Workspace document save unavailable", CommandOutputChannel::error);
            }

            auto result = document_save_handler_(id);
            if (result.ok()) {
                workspace_->set_tab_dirty(id, false);
            }
            return result;
        });

    register_or_log(CommandSpec{
                        "workspace.save_and_close_tab",
                        "Save and Close Workspace Tab",
                        "Save a dirty workspace tab and close it only after save succeeds",
                        "workspace",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "workspace.save_and_close_tab <id>",
                    },
        [this](const CommandInvocation& invocation, CommandContext context) {
            const std::string id = command_arg_string(invocation.args, 0);
            if (id.empty()) {
                return command_result(CommandStatus::rejected, "Workspace tab id required", CommandOutputChannel::warn);
            }

            auto save = execute_command("workspace.save_tab", {id}, context);
            if (!save.ok()) {
                return save;
            }

            return execute_command("workspace.close_tab", {id}, context);
        });

    register_or_log(CommandSpec{
                        "workspace.move_tab",
                        "Move Workspace Tab",
                        "Move a workspace tab to a new index",
                        "workspace",
                        {"tab.move"},
                        CommandScope::global,
                        CommandFlags::hidden,
                        {},
                        "workspace.move_tab <id> <index>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const std::string id = command_arg_string(invocation.args, 0);
            const std::string index_text = command_arg_string(invocation.args, 1);
            if (id.empty() || index_text.empty()) {
                return command_result(CommandStatus::rejected, "Workspace tab id and target index required", CommandOutputChannel::warn);
            }
            char* end = nullptr;
            const auto target_index = static_cast<size_t>(std::strtoull(index_text.c_str(), &end, 10));
            if (!end || *end != '\0') {
                return command_result(CommandStatus::rejected, "Workspace tab target index must be numeric", CommandOutputChannel::warn);
            }
            if (!workspace_->move_tab(id, target_index)) {
                return command_result(CommandStatus::noop, "Workspace tab not moved", CommandOutputChannel::none);
            }
            return command_result(CommandStatus::success, "Workspace tab moved", CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "workspace.open_subtab",
                        "Open Workspace Sub-Tab",
                        "Open a sub-tab below a workspace tab",
                        "workspace",
                        {"subtab.open"},
                        CommandScope::global,
                        CommandFlags::hidden,
                        {},
                        "workspace.open_subtab <tab-id> <subtab-id> [title]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            std::string tab_id = command_arg_string(invocation.args, 0);
            const std::string subtab_id = command_arg_string(invocation.args, 1);
            std::string title = command_arg_string(invocation.args, 2);
            if (tab_id.empty()) {
                tab_id = workspace_->active_tab_id();
            }
            if (tab_id.empty() || subtab_id.empty()) {
                return command_result(CommandStatus::rejected, "Workspace tab id and sub-tab id required", CommandOutputChannel::warn);
            }
            if (!workspace_->open_subtab(tab_id, subtab_id, std::move(title))) {
                return command_result(CommandStatus::rejected, "Workspace sub-tab not opened", CommandOutputChannel::warn);
            }
            return command_result(CommandStatus::success, "Workspace sub-tab opened", CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "workspace.activate_subtab",
                        "Activate Workspace Sub-Tab",
                        "Activate a sub-tab below a workspace tab",
                        "workspace",
                        {"subtab.activate"},
                        CommandScope::global,
                        CommandFlags::hidden,
                        {},
                        "workspace.activate_subtab <tab-id> <subtab-id>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            std::string tab_id = command_arg_string(invocation.args, 0);
            const std::string subtab_id = command_arg_string(invocation.args, 1);
            if (tab_id.empty()) {
                tab_id = workspace_->active_tab_id();
            }
            if (tab_id.empty() || subtab_id.empty() || !workspace_->set_active_subtab(tab_id, subtab_id)) {
                return command_result(CommandStatus::rejected, "Workspace sub-tab not activated", CommandOutputChannel::warn);
            }
            return command_result(CommandStatus::success, "Workspace sub-tab activated", CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "workspace.close_subtab",
                        "Close Workspace Sub-Tab",
                        "Close a sub-tab below a workspace tab",
                        "workspace",
                        {"subtab.close"},
                        CommandScope::global,
                        CommandFlags::hidden,
                        {},
                        "workspace.close_subtab [tab-id] [subtab-id]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            std::string tab_id = command_arg_string(invocation.args, 0);
            std::string subtab_id = command_arg_string(invocation.args, 1);
            if (tab_id.empty()) {
                tab_id = workspace_->active_tab_id();
            }
            if (subtab_id.empty() && tab_id == workspace_->active_tab_id()) {
                if (const auto* active_subtab = workspace_->active_subtab()) {
                    subtab_id = active_subtab->id;
                }
            }
            if (tab_id.empty() || subtab_id.empty() || !workspace_->close_subtab(tab_id, subtab_id)) {
                return command_result(CommandStatus::noop, "No workspace sub-tab closed", CommandOutputChannel::none);
            }
            return command_result(CommandStatus::success, "Workspace sub-tab closed", CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.palette.toggle",
                        "Toggle Command Palette",
                        "Show or hide the command palette",
                        "shell",
                        {"palette"},
                        CommandScope::global,
                        CommandFlags::none,
                        "Ctrl+Shift+P",
                        "rollnw.client.palette.toggle",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            shell_->set_command_palette_visible(!shell_->command_palette_visible);
            return command_result(CommandStatus::success,
                shell_->command_palette_visible ? "Command palette shown" : "Command palette hidden",
                CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.terminal.toggle",
                        "Toggle Terminal",
                        "Show or hide the command terminal",
                        "shell",
                        {"terminal"},
                        CommandScope::global,
                        CommandFlags::none,
                        "`",
                        "rollnw.client.terminal.toggle",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            shell_->set_terminal_visible(!shell_->terminal_visible());
            return command_result(CommandStatus::success,
                shell_->terminal_visible() ? "Terminal shown" : "Terminal hidden",
                CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.terminal.clear",
                        "Clear Terminal",
                        "Clear the command terminal output",
                        "shell",
                        {"clear", "cls", "terminal.clear"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "clear",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            shell_->clear_terminal();
            return command_result(CommandStatus::success, {}, CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.output.toggle",
                        "Toggle Output Log",
                        "Show or hide the passive output log",
                        "shell",
                        {"output"},
                        CommandScope::global,
                        CommandFlags::none,
                        "Ctrl+J",
                        "rollnw.client.output.toggle",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            shell_->set_output_panel_visible(!shell_->output_panel_visible());
            return command_result(CommandStatus::success,
                shell_->output_panel_visible() ? "Output log shown" : "Output log hidden",
                CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "area.toggle_lights",
                        "Toggle Area Lights",
                        "Turn authored area lights on or off in 3D viewports",
                        "viewer",
                        {},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.toggle_lights",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            shell_->viewer_area_lights_enabled = !shell_->viewer_area_lights_enabled;
            return command_result(CommandStatus::success,
                shell_->viewer_area_lights_enabled ? "Area lights enabled" : "Area lights disabled",
                CommandOutputChannel::info);
        });

    register_area_toggle(CommandSpec{
                             "area.toggle_debug",
                             "Toggle Area Debug",
                             "Show or hide area debug overlays in 3D viewports",
                             "viewer",
                             {},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_debug",
                         },
        &ShellController::viewer_area_debug_enabled, "Area debug enabled", "Area debug disabled");

    register_area_toggle(CommandSpec{
                             "area.toggle_forward_plus",
                             "Toggle Forward+",
                             "Turn Forward+ local-light clustering on or off in 3D viewports",
                             "viewer",
                             {"area.fplus"},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_forward_plus",
                         },
        &ShellController::viewer_forward_plus_enabled, "Forward+ enabled", "Forward+ disabled");

    register_or_log(CommandSpec{
                        "area.set_forward_plus_config",
                        "Set Forward+ Config",
                        "Use a fixed Forward+ cluster config in 3D viewports",
                        "viewer",
                        {},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.set_forward_plus_config <tile-size> <depth-slices> [max-lights-per-cluster]",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            const auto tile_size = parse_positive_u32(command_arg_string(invocation.args, 0));
            const auto depth_slices = parse_positive_u32(command_arg_string(invocation.args, 1));
            const std::string max_lights_text = command_arg_string(invocation.args, 2);
            const auto max_lights = max_lights_text.empty()
                ? std::optional<uint32_t>{128u}
                : parse_positive_u32(max_lights_text);
            if (!tile_size || !depth_slices || !max_lights) {
                return command_result(CommandStatus::rejected,
                    "Usage: area.set_forward_plus_config <tile-size> <depth-slices> [max-lights-per-cluster]",
                    CommandOutputChannel::warn);
            }

            shell_->viewer_forward_plus_enabled = true;
            shell_->viewer_forward_plus_auto_configure_area = false;
            shell_->viewer_forward_plus_tile_size = *tile_size;
            shell_->viewer_forward_plus_depth_slices = *depth_slices;
            shell_->viewer_forward_plus_max_lights_per_cluster = *max_lights;
            return command_result(CommandStatus::success,
                fmt::format(
                    "Forward+ config: tile={} depth={} max={}",
                    *tile_size,
                    *depth_slices,
                    *max_lights),
                CommandOutputChannel::info);
        });

    register_or_log(CommandSpec{
                        "area.reset_forward_plus_config",
                        "Reset Forward+ Config",
                        "Return area Forward+ clustering to automatic config",
                        "viewer",
                        {},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.reset_forward_plus_config",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            shell_->viewer_forward_plus_enabled = true;
            shell_->viewer_forward_plus_auto_configure_area = true;
            shell_->viewer_forward_plus_tile_size = 64u;
            shell_->viewer_forward_plus_depth_slices = 8u;
            shell_->viewer_forward_plus_max_lights_per_cluster = 128u;
            return command_result(CommandStatus::success, "Forward+ config: automatic", CommandOutputChannel::info);
        });

    register_or_log(CommandSpec{
                        "area.cycle_forward_plus_debug",
                        "Cycle Forward+ Debug",
                        "Cycle the Forward+ debug heatmap shown in 3D viewports",
                        "viewer",
                        {"area.forward_plus_debug", "fplusdebug"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.cycle_forward_plus_debug",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            shell_->viewer_forward_plus_debug_mode = next_forward_plus_debug_mode(shell_->viewer_forward_plus_debug_mode);
            return command_result(CommandStatus::success,
                std::string{"Forward+ debug: "}
                    + forward_plus_debug_mode_label(shell_->viewer_forward_plus_debug_mode),
                CommandOutputChannel::info);
        });

    register_or_log(CommandSpec{
                        "area.set_forward_plus_debug",
                        "Set Forward+ Debug",
                        "Set the Forward+ debug heatmap shown in 3D viewports",
                        "viewer",
                        {},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.set_forward_plus_debug <off|cluster-lights|depth-slices>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            const std::string mode_text = command_arg_string(invocation.args, 0);
            const auto mode = parse_forward_plus_debug_mode(mode_text);
            if (!mode) {
                return command_result(CommandStatus::rejected,
                    "Usage: area.set_forward_plus_debug <off|cluster-lights|depth-slices>",
                    CommandOutputChannel::warn);
            }

            shell_->viewer_forward_plus_debug_mode = *mode;
            return command_result(CommandStatus::success,
                std::string{"Forward+ debug: "} + forward_plus_debug_mode_label(*mode),
                CommandOutputChannel::info);
        });

    register_area_toggle(CommandSpec{
                             "area.toggle_triggers",
                             "Toggle Area Triggers",
                             "Show or hide area trigger debug geometry in 3D viewports",
                             "viewer",
                             {},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_triggers",
                         },
        &ShellController::viewer_area_triggers_enabled, "Area triggers shown", "Area triggers hidden");

    register_area_toggle(CommandSpec{
                             "area.toggle_encounters",
                             "Toggle Area Encounters",
                             "Show or hide area encounter debug geometry in 3D viewports",
                             "viewer",
                             {},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_encounters",
                         },
        &ShellController::viewer_area_encounters_enabled, "Area encounters shown", "Area encounters hidden");

    register_area_toggle(CommandSpec{
                             "area.toggle_fog",
                             "Toggle Area Fog",
                             "Show or hide authored area fog in 3D viewports",
                             "viewer",
                             {},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_fog",
                         },
        &ShellController::viewer_area_fog_enabled, "Area fog enabled", "Area fog disabled");

    register_area_toggle(CommandSpec{
                             "area.toggle_shadows",
                             "Toggle Area Shadows",
                             "Turn area shadows on or off in 3D viewports",
                             "viewer",
                             {},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_shadows",
                         },
        &ShellController::viewer_area_shadows_enabled, "Area shadows enabled", "Area shadows disabled");

    register_area_toggle(CommandSpec{
                             "area.toggle_time",
                             "Toggle Area Time",
                             "Play or pause the area day-night cycle in 3D viewports",
                             "viewer",
                             {},
                             CommandScope::global,
                             CommandFlags::none,
                             {},
                             "area.toggle_time",
                         },
        &ShellController::viewer_area_day_night_autoplay, "Area time cycle playing", "Area time cycle paused");

    register_or_log(CommandSpec{
                        "area.set_time",
                        "Set Area Time",
                        "Set the area day-night cycle hour and pause autoplay",
                        "viewer",
                        {},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.set_time <hour>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            const std::string hour_text = command_arg_string(invocation.args, 0);
            if (hour_text.empty()) {
                return command_result(CommandStatus::rejected,
                    "Usage: area.set_time <hour>",
                    CommandOutputChannel::warn);
            }

            errno = 0;
            char* end = nullptr;
            const float hour = std::strtof(hour_text.c_str(), &end);
            while (end && std::isspace(static_cast<unsigned char>(*end))) {
                ++end;
            }
            if (end == hour_text.c_str() || (end && *end != '\0') || errno == ERANGE
                || !std::isfinite(hour) || hour < 0.0f || hour > 24.0f) {
                return command_result(CommandStatus::rejected,
                    "Area time must be an hour from 0 to 24",
                    CommandOutputChannel::warn);
            }

            const float normalized_hour = hour >= 24.0f ? 0.0f : hour;
            shell_->viewer_area_day_night_elapsed_seconds = (normalized_hour / 24.0f) * nw::render::viewer::kAreaDayNightCycleSeconds;
            shell_->viewer_area_day_night_autoplay = false;
            ++shell_->viewer_area_day_night_time_generation;
            return command_result(CommandStatus::success,
                std::string{"Area time set to "} + hour_text,
                CommandOutputChannel::info);
        });

    register_or_log(CommandSpec{
                        "area.reload",
                        "Reload Area",
                        "Reload the active area in 3D viewports",
                        "viewer",
                        {},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "area.reload",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            ++shell_->viewer_area_reload_generation;
            return command_result(CommandStatus::success, "Area reload requested", CommandOutputChannel::info);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.dock.toggle",
                        "Toggle Dock",
                        "Show or hide a shell dock",
                        "shell",
                        {"dock.toggle"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "rollnw.client.dock.toggle <bottom>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            DockRegion region = DockRegion::bottom;
            const std::string region_name = command_arg_string(invocation.args, 0);
            if (!region_name.empty() && !dock_region_from_string(region_name, region)) {
                return command_result(CommandStatus::rejected,
                    std::string{"Unknown dock: "} + region_name,
                    CommandOutputChannel::warn);
            }
            if (region != DockRegion::bottom) {
                return command_result(CommandStatus::rejected,
                    std::string{"Dock not wired yet: "} + std::string(dock_region_name(region)),
                    CommandOutputChannel::warn);
            }

            shell_->set_bottom_dock_visible(!shell_->bottom_dock_visible());
            return command_result(CommandStatus::success,
                shell_->bottom_dock_visible() ? "Bottom dock shown" : "Bottom dock hidden",
                CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.dock.activate",
                        "Activate Dock Widget",
                        "Show a widget in a shell dock",
                        "shell",
                        {"dock.activate"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "rollnw.client.dock.activate <bottom> <output|terminal>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }

            DockRegion region = DockRegion::bottom;
            const std::string region_name = command_arg_string(invocation.args, 0);
            const std::string widget = command_arg_string(invocation.args, 1);
            if (!dock_region_from_string(region_name, region)) {
                return command_result(CommandStatus::rejected,
                    std::string{"Unknown dock: "} + region_name,
                    CommandOutputChannel::warn);
            }
            if (region != DockRegion::bottom) {
                return command_result(CommandStatus::rejected,
                    std::string{"Dock not wired yet: "} + std::string(dock_region_name(region)),
                    CommandOutputChannel::warn);
            }
            if (!shell_->activate_bottom_dock_widget(widget)) {
                return command_result(CommandStatus::rejected,
                    std::string{"Unknown bottom dock widget: "} + widget,
                    CommandOutputChannel::warn);
            }
            return command_result(CommandStatus::success, std::string{"Bottom dock: "} + widget, CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "rollnw.client.output.channel",
                        "Toggle Output Channel",
                        "Show or hide an output log channel",
                        "shell",
                        {"output.channel", "output.toggle"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "rollnw.client.output.channel <info|warn|error|script>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!shell_) {
                return command_result(CommandStatus::failed, "Shell unavailable", CommandOutputChannel::error);
            }
            const std::string channel = command_arg_string(invocation.args, 0);
            if (!shell_->toggle_output_channel(channel)) {
                return command_result(CommandStatus::rejected,
                    std::string{"Unknown output channel: "} + channel,
                    CommandOutputChannel::warn);
            }
            return command_result(CommandStatus::success, std::string{"Output channel toggled: "} + channel, CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "toolset.open",
                        "Open Module",
                        "Open a module and list its areas",
                        "module",
                        {"open", "toolset.open_module"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "toolset.open <path>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            return open_module(command_arg_string(invocation.args, 0));
        });

    register_or_log(CommandSpec{
                        "toolset.open_project",
                        "Open Project",
                        "Open an Client project directory",
                        "project",
                        {"project.open", "open_project"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "toolset.open_project <path>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            return open_project(command_arg_string(invocation.args, 0));
        });

    register_or_log(CommandSpec{
                        "toolset.open_resource",
                        "Open Project Resource",
                        "Open a resource from the active Client project",
                        "project",
                        {"resource.open", "open_resource"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "toolset.open_resource <project-relative-path>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            if (current_project_dir_.empty()) {
                return command_result(CommandStatus::rejected, "No project is open", CommandOutputChannel::warn);
            }

            const std::string requested_path = command_arg_string(invocation.args, 0);
            if (requested_path.empty()) {
                return command_result(CommandStatus::rejected, "Resource path required", CommandOutputChannel::warn);
            }

            namespace fs = std::filesystem;
            const fs::path input_path{requested_path};
            const fs::path target_path = input_path.is_absolute()
                ? input_path
                : current_project_dir_ / input_path;

            std::error_code ec;
            if (!fs::exists(target_path, ec)) {
                return command_result(CommandStatus::failed,
                    std::string{"Resource does not exist: "} + target_path.string(),
                    CommandOutputChannel::error);
            }
            if (fs::is_directory(target_path, ec)) {
                return command_result(CommandStatus::noop,
                    std::string{"Project folder: "} + fs::relative(target_path, current_project_dir_, ec).generic_string(),
                    CommandOutputChannel::info);
            }

            const fs::path canonical_root = fs::weakly_canonical(current_project_dir_, ec);
            if (ec) {
                return command_result(CommandStatus::failed,
                    std::string{"Failed to resolve project path: "} + current_project_dir_.string(),
                    CommandOutputChannel::error);
            }
            const fs::path canonical_target = fs::weakly_canonical(target_path, ec);
            if (ec) {
                return command_result(CommandStatus::failed,
                    std::string{"Failed to resolve resource path: "} + target_path.string(),
                    CommandOutputChannel::error);
            }

            const fs::path relative = fs::relative(canonical_target, canonical_root, ec);
            if (ec || relative.empty() || relative_path_escapes_root(relative)) {
                return command_result(CommandStatus::rejected,
                    std::string{"Resource is outside the current project: "} + target_path.string(),
                    CommandOutputChannel::warn);
            }

            const std::string relative_text = relative.generic_string();
            const std::string title = project_resource_display_name(current_project_dir_, relative);
            if (project_resource_is_area(relative)) {
                workspace_->open_or_replace_tab("area", title, WorkspaceTabKind::area, relative_text, false, false);
                return command_result(CommandStatus::success, std::string{"Opened area: "} + relative_text);
            }
            if (project_resource_is_preview_blueprint(relative)) {
                workspace_->open_or_replace_tab(
                    std::string{"preview:"} + relative_text, title, WorkspaceTabKind::preview, relative_text);
                return command_result(CommandStatus::success, std::string{"Opened preview: "} + relative_text);
            }
            if (project_resource_is_dialog(relative)) {
                workspace_->open_or_replace_tab(
                    std::string{"dialog:"} + relative_text, title, WorkspaceTabKind::dialog, relative_text);
                return command_result(CommandStatus::success, std::string{"Opened dialog: "} + relative_text);
            }

            workspace_->open_or_replace_tab(
                std::string{"resource:"} + relative_text, title, WorkspaceTabKind::resource, relative_text);
            return command_result(CommandStatus::success, std::string{"Opened resource: "} + relative_text);
        });

    register_or_log(CommandSpec{
                        "toolset.select_area",
                        "Select Area",
                        "Select an area by resource reference",
                        "area",
                        {"area"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "toolset.select_area <resref>",
                    },
        [this](const CommandInvocation& invocation, CommandContext&) {
            const std::string resref = command_arg_string(invocation.args, 0);
            if (resref.empty()) {
                return command_result(CommandStatus::rejected, "Area resref required", CommandOutputChannel::warn);
            }
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const auto area = std::find_if(loaded_areas_.begin(), loaded_areas_.end(), [&resref](const LoadedAreaEntry& entry) {
                return entry.resref == resref;
            });
            if (area == loaded_areas_.end()) {
                return command_result(CommandStatus::rejected,
                    std::string{"Unknown area: "} + resref,
                    CommandOutputChannel::warn);
            }

            const std::string title{display_name_or_resref(*area)};
            const std::string resource = area->resource.empty()
                ? area->resref + ".are"
                : area->resource;
            workspace_->open_or_replace_tab("area", title, WorkspaceTabKind::area, resource, false, false);
            return command_result(CommandStatus::success, std::string{"Opened area: "} + resref);
        });

    register_or_log(CommandSpec{
                        "object.details.set_boolean",
                        "Set Object Details Boolean",
                        "Set one explicitly editable boolean in the active object's Details",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.details.set_boolean <row-index> <expected-0|1> <desired-0|1>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto row_index = parse_u32(command_arg_string(invocation.args, 0));
            const auto expected = parse_assignment(command_arg_string(invocation.args, 1));
            const auto desired = parse_assignment(command_arg_string(invocation.args, 2));
            if (!row_index || !expected || !desired) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.details.set_boolean <row-index> <expected-0|1> <desired-0|1>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(
                    CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            std::string diagnostic;
            auto edit = prepare_object_details_boolean_edit(kernel::runtime(),
                bridge_->active_object(), *row_index, *expected ? 1 : 0, *desired, diagnostic);
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    diagnostic.empty() ? "Object Details boolean edit was rejected" : std::move(diagnostic),
                    CommandOutputChannel::warn);
            }

            ObjectEditBatch batch;
            batch.patches.push_back({
                edit->object,
                edit->propset_type,
                edit->field_index,
                edit->before,
                edit->after,
                edit->element_index,
            });
            return commit_object_edits(std::move(batch), std::move(edit->label), context);
        });

    register_or_log(CommandSpec{
                        "object.details.set_integer",
                        "Set Object Details Integer",
                        "Set one explicitly ranged integer in the active object's Details",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.details.set_integer <row-index> <expected> <desired>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto row_index = parse_u32(command_arg_string(invocation.args, 0));
            const auto expected = parse_i32(command_arg_string(invocation.args, 1));
            const auto desired = parse_i32(command_arg_string(invocation.args, 2));
            if (!row_index || !expected || !desired) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.details.set_integer <row-index> <expected> <desired>",
                    CommandOutputChannel::warn);
            }
            if (*expected == *desired) {
                return command_result(CommandStatus::noop,
                    "Object Details integer is already set", CommandOutputChannel::none);
            }
            if (!bridge_) {
                return command_result(
                    CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            std::string diagnostic;
            auto edit = prepare_object_details_integer_edit(kernel::runtime(),
                bridge_->active_object(), *row_index, *expected, *desired, diagnostic);
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    diagnostic.empty() ? "Object Details integer edit was rejected" : std::move(diagnostic),
                    CommandOutputChannel::warn);
            }

            ObjectEditBatch batch;
            batch.kind = edit->element_index == -1
                ? ObjectEditKind::propset_int
                : ObjectEditKind::propset_int_element;
            batch.patches.push_back({
                edit->object,
                edit->propset_type,
                edit->field_index,
                edit->before,
                edit->after,
                edit->element_index,
            });
            return commit_object_edits(std::move(batch), std::move(edit->label), context);
        });

    register_or_log(CommandSpec{
                        "object.variables.add",
                        "Add Object Variable",
                        "Add one integer variable to the active object",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.variables.add",
                    },
        [this](const CommandInvocation&, CommandContext& context) {
            if (!bridge_) {
                return command_result(
                    CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }
            const ObjectHandle object = bridge_->active_object();
            ObjectVariableSnapshot snapshot;
            snapshot_object_variables(object, snapshot);
            if (snapshot.status != ObjectVariableSnapshotStatus::ready) {
                return command_result(CommandStatus::rejected,
                    snapshot.diagnostic.empty()
                        ? "Object variable data is unavailable"
                        : std::move(snapshot.diagnostic),
                    CommandOutputChannel::warn);
            }

            ObjectVariableEditBatch batch;
            batch.object = object;
            batch.kind = ObjectVariableEditKind::insert;
            batch.rows.push_back({
                .after = default_object_variable_record(
                    "variable", ObjectVariableType::integer),
            });
            return commit_object_variable_edits(
                std::move(batch), "Add Object variable", context);
        });

    register_or_log(CommandSpec{
                        "object.variables.remove",
                        "Remove Object Variable",
                        "Remove one variable from the active object",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.variables.remove <name> <type>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const std::string name = command_arg_string(invocation.args, 0);
            const auto type = parse_object_variable_type(
                command_arg_string(invocation.args, 1));
            if (name.empty() || !type || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.variables.remove <name> <integer|float|string>",
                    CommandOutputChannel::warn);
            }
            std::string diagnostic;
            auto before = find_object_variable(
                bridge_->active_object(), name, *type, diagnostic);
            if (!before) {
                return command_result(CommandStatus::rejected,
                    std::move(diagnostic), CommandOutputChannel::warn);
            }

            ObjectVariableEditBatch batch;
            batch.object = bridge_->active_object();
            batch.kind = ObjectVariableEditKind::erase;
            batch.rows.push_back({.before = std::move(*before)});
            return commit_object_variable_edits(
                std::move(batch), "Remove Object variable", context);
        });

    register_or_log(CommandSpec{
                        "object.variables.rename",
                        "Rename Object Variable",
                        "Rename one variable on the active object",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.variables.rename <name> <type> <new-name>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const std::string name = command_arg_string(invocation.args, 0);
            const auto type = parse_object_variable_type(
                command_arg_string(invocation.args, 1));
            const std::string desired = command_arg_string(invocation.args, 2);
            if (name.empty() || !type || desired.empty() || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.variables.rename <name> <integer|float|string> <new-name>",
                    CommandOutputChannel::warn);
            }
            if (name == desired) {
                return command_result(CommandStatus::noop,
                    "Object variable name is already set", CommandOutputChannel::none);
            }
            std::string diagnostic;
            auto before = find_object_variable(
                bridge_->active_object(), name, *type, diagnostic);
            if (!before) {
                return command_result(CommandStatus::rejected,
                    std::move(diagnostic), CommandOutputChannel::warn);
            }
            auto after = *before;
            after.name = desired;

            ObjectVariableEditBatch batch;
            batch.object = bridge_->active_object();
            batch.kind = ObjectVariableEditKind::replace;
            batch.rows.push_back({std::move(*before), std::move(after)});
            return commit_object_variable_edits(
                std::move(batch), "Rename Object variable", context);
        });

    register_or_log(CommandSpec{
                        "object.variables.set_type",
                        "Set Object Variable Type",
                        "Change one variable type on the active object",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.variables.set_type <name> <expected-type> <desired-type>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const std::string name = command_arg_string(invocation.args, 0);
            const auto expected_type = parse_object_variable_type(
                command_arg_string(invocation.args, 1));
            const auto desired_type = parse_object_variable_type(
                command_arg_string(invocation.args, 2));
            if (name.empty() || !expected_type || !desired_type || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.variables.set_type <name> <expected-type> <desired-type>",
                    CommandOutputChannel::warn);
            }
            if (*expected_type == *desired_type) {
                return command_result(CommandStatus::noop,
                    "Object variable type is already set", CommandOutputChannel::none);
            }
            std::string diagnostic;
            auto before = find_object_variable(
                bridge_->active_object(), name, *expected_type, diagnostic);
            if (!before) {
                return command_result(CommandStatus::rejected,
                    std::move(diagnostic), CommandOutputChannel::warn);
            }
            auto after = default_object_variable_record(name, *desired_type);

            ObjectVariableEditBatch batch;
            batch.object = bridge_->active_object();
            batch.kind = ObjectVariableEditKind::replace;
            batch.rows.push_back({std::move(*before), std::move(after)});
            return commit_object_variable_edits(
                std::move(batch), "Change Object variable type", context);
        });

    register_or_log(CommandSpec{
                        "object.variables.set_value",
                        "Set Object Variable Value",
                        "Set one variable value on the active object",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::hidden,
                        {},
                        "object.variables.set_value <name> <type> <value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const std::string name = command_arg_string(invocation.args, 0);
            const auto type = parse_object_variable_type(
                command_arg_string(invocation.args, 1));
            const std::string desired = command_arg_string(invocation.args, 2);
            if (name.empty() || !type || invocation.args.size() < 3 || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.variables.set_value <name> <integer|float|string> <value>",
                    CommandOutputChannel::warn);
            }
            std::string diagnostic;
            auto before = find_object_variable(
                bridge_->active_object(), name, *type, diagnostic);
            if (!before) {
                return command_result(CommandStatus::rejected,
                    std::move(diagnostic), CommandOutputChannel::warn);
            }
            auto after = *before;
            switch (*type) {
            case ObjectVariableType::integer: {
                const auto value = parse_i32(desired);
                if (!value) {
                    return command_result(CommandStatus::rejected,
                        "Integer variable value must be in the signed 32-bit range",
                        CommandOutputChannel::warn);
                }
                after.integer = *value;
                break;
            }
            case ObjectVariableType::floating: {
                const auto value = parse_finite_f32(desired);
                if (!value) {
                    return command_result(CommandStatus::rejected,
                        "Float variable value must be finite and representable as float32",
                        CommandOutputChannel::warn);
                }
                after.floating = *value;
                break;
            }
            case ObjectVariableType::string:
                after.string = desired;
                break;
            }

            ObjectVariableEditBatch batch;
            batch.object = bridge_->active_object();
            batch.kind = ObjectVariableEditKind::replace;
            batch.rows.push_back({std::move(*before), std::move(after)});
            return commit_object_variable_edits(
                std::move(batch), "Set Object variable value", context);
        });

    register_or_log(CommandSpec{
                        "object.transform.set_position",
                        "Set Object Position",
                        "Set the active Creature or Placeable position",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.transform.set_position <x> <y> <z>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto x = parse_finite_f32(command_arg_string(invocation.args, 0));
            const auto y = parse_finite_f32(command_arg_string(invocation.args, 1));
            const auto z = parse_finite_f32(command_arg_string(invocation.args, 2));
            if (!x || !y || !z) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.transform.set_position <x> <y> <z>",
                    CommandOutputChannel::warn);
            }
            const auto active = active_transform(bridge_);
            if (!active) {
                return command_result(CommandStatus::rejected,
                    "Active object is not an editable Creature or Placeable",
                    CommandOutputChannel::warn);
            }

            auto after = active->state;
            after.position = {*x, *y, *z};
            if (after.position == active->state.position) {
                return command_result(CommandStatus::noop, "Object position is unchanged", CommandOutputChannel::none);
            }
            return commit_object_transform_edit(
                {active->object, active->state, after}, "Move object", context);
        });

    register_or_log(CommandSpec{
                        "object.transform.rotate",
                        "Rotate Object",
                        "Rotate the active Creature or Placeable by degrees",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.transform.rotate <degrees>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto degrees = parse_finite_f32(command_arg_string(invocation.args, 0));
            if (!degrees) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.transform.rotate <degrees>",
                    CommandOutputChannel::warn);
            }
            const auto active = active_transform(bridge_);
            if (!active) {
                return command_result(CommandStatus::rejected,
                    "Active object is not an editable Creature or Placeable",
                    CommandOutputChannel::warn);
            }
            if (*degrees == 0.0f) {
                return command_result(CommandStatus::noop, "Object orientation is unchanged", CommandOutputChannel::none);
            }

            auto after = active->state;
            after.orientation = orientation_from_angle(
                orientation_angle(after.orientation) + *degrees * std::numbers::pi_v<float> / 180.0f);
            return commit_object_transform_edit(
                {active->object, active->state, after}, "Rotate object", context);
        });

    register_or_log(CommandSpec{
                        "object.transform.randomize_orientation",
                        "Randomize Object Orientation",
                        "Set a random facing for the active Creature or Placeable",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        "R",
                        "object.transform.randomize_orientation",
                    },
        [this](const CommandInvocation&, CommandContext& context) {
            const auto active = active_transform(bridge_);
            if (!active) {
                return command_result(CommandStatus::rejected,
                    "Active object is not an editable Creature or Placeable",
                    CommandOutputChannel::warn);
            }

            auto after = active->state;
            after.orientation = orientation_from_angle(random_orientation_angle());
            return commit_object_transform_edit(
                {active->object, active->state, after}, "Randomize object orientation", context);
        });

    register_or_log(CommandSpec{
                        "object.transform.scale",
                        "Scale Object",
                        "Multiply the active Creature or Placeable scale",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.transform.scale <positive-factor>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto factor = parse_finite_f32(command_arg_string(invocation.args, 0));
            if (!factor || *factor <= 0.0f) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.transform.scale <positive-factor>",
                    CommandOutputChannel::warn);
            }
            const auto active = active_transform(bridge_);
            if (!active) {
                return command_result(CommandStatus::rejected,
                    "Active object is not an editable Creature or Placeable",
                    CommandOutputChannel::warn);
            }
            if (*factor == 1.0f) {
                return command_result(CommandStatus::noop, "Object scale is unchanged", CommandOutputChannel::none);
            }

            auto after = active->state;
            after.scale *= *factor;
            if (!std::isfinite(after.scale.x) || !std::isfinite(after.scale.y) || !std::isfinite(after.scale.z)
                || after.scale.x <= 0.0f || after.scale.y <= 0.0f || after.scale.z <= 0.0f) {
                return command_result(CommandStatus::rejected,
                    "Object scale factor is outside the positive finite range",
                    CommandOutputChannel::warn);
            }
            return commit_object_transform_edit(
                {active->object, active->state, after}, "Scale object", context);
        });

    register_or_log(CommandSpec{
                        "area.object.duplicate",
                        "Duplicate Area Object",
                        "Duplicate the active Creature or Placeable in the live area",
                        "object",
                        {"duplicate"},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "area.object.duplicate",
                    },
        [this](const CommandInvocation&, CommandContext& context) {
            if (!bridge_) {
                return command_result(CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }
            const std::array objects{bridge_->active_object()};
            const ObjectHandle area = context.area_object.type == ObjectType::area
                ? context.area_object
                : bridge_->active_area();
            return duplicate_area_objects(
                area, objects, "Duplicate area object", context);
        });

    register_or_log(CommandSpec{
                        "area.object.delete",
                        "Delete Area Object",
                        "Delete the active Creature or Placeable from the live area",
                        "object",
                        {"delete"},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "area.object.delete",
                    },
        [this](const CommandInvocation&, CommandContext& context) {
            if (!bridge_) {
                return command_result(CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }
            const std::array objects{bridge_->active_object()};
            const ObjectHandle area = context.area_object.type == ObjectType::area
                ? context.area_object
                : bridge_->active_area();
            return delete_area_objects(
                area, objects, "Delete area object", context);
        });

    register_or_log(CommandSpec{
                        "object.set_appearance",
                        "Set Object Appearance",
                        "Set the active Creature or Placeable appearance",
                        "object",
                        {"appearance"},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.set_appearance <appearance-id>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto appearance = parse_u32(command_arg_string(invocation.args, 0));
            if (!appearance || *appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.set_appearance <appearance-id>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::creature && object.type != ObjectType::placeable) {
                return command_result(
                    CommandStatus::rejected, "Active object has no editable appearance", CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = object_appearance(runtime, object);
            if (!current) {
                return command_result(CommandStatus::failed, "Object appearance is unavailable", CommandOutputChannel::error);
            }
            const int32_t desired = static_cast<int32_t>(*appearance);
            if (*current == desired) {
                return command_result(CommandStatus::noop, "Object appearance is already set", CommandOutputChannel::none);
            }

            auto edit = make_object_appearance_edit(runtime, object, desired);
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Object appearance edit could not be prepared",
                    CommandOutputChannel::warn);
            }
            return commit_object_appearance_edit(
                std::move(*edit), "Set object appearance", context);
        });

    register_or_log(CommandSpec{
                        "object.door.set_appearance",
                        "Set Door Appearance",
                        "Set the active Door appearance selector pair through Smalls policy",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.door.set_appearance <appearance-id> <generic-type-id>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto appearance = parse_u32(command_arg_string(invocation.args, 0));
            const auto generic_type = parse_u32(command_arg_string(invocation.args, 1));
            constexpr auto max_selector = static_cast<uint32_t>(
                std::numeric_limits<int32_t>::max());
            if (!appearance || !generic_type
                || *appearance > max_selector || *generic_type > max_selector) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.door.set_appearance <appearance-id> <generic-type-id>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::door) {
                return command_result(CommandStatus::rejected,
                    "Active object is not a Door", CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = door_appearance(runtime, object);
            if (!current) {
                return command_result(CommandStatus::failed,
                    "Door appearance is unavailable", CommandOutputChannel::error);
            }
            const ObjectAppearanceSelectors desired{
                static_cast<int32_t>(*appearance),
                static_cast<int32_t>(*generic_type),
            };
            if (*current == desired) {
                return command_result(CommandStatus::noop,
                    "Door appearance is already set", CommandOutputChannel::none);
            }

            auto edit = make_door_appearance_edit(
                runtime, object, desired.appearance, desired.generic_type);
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Door appearance edit could not be prepared",
                    CommandOutputChannel::warn);
            }
            return commit_object_appearance_edit(
                std::move(*edit), "Set Door appearance", context);
        });

    register_or_log(CommandSpec{
                        "object.item.set_model_part",
                        "Set Item Model Part",
                        "Set one active Item visual model part through Smalls policy",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.item.set_model_part <part-id> <model-value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto part = parse_i32(command_arg_string(invocation.args, 0));
            const auto value = parse_i32(command_arg_string(invocation.args, 1));
            if (!part || !value || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.item.set_model_part <part-id> <model-value>",
                    CommandOutputChannel::warn);
            }
            const ObjectHandle object = bridge_->active_object();
            const std::array parts{*part};
            const std::array values{*value};
            auto batch = make_item_model_part_edits(
                kernel::runtime(), object, parts, values);
            if (!batch) {
                return command_result(CommandStatus::rejected,
                    "Active Item does not expose that model part",
                    CommandOutputChannel::warn);
            }
            if (batch->patches.front().before == *value) {
                return command_result(CommandStatus::noop,
                    "Item model part is already set", CommandOutputChannel::none);
            }
            return commit_object_edits(
                std::move(*batch), "Set Item model part", context);
        });

    register_or_log(CommandSpec{
                        "object.item.set_color",
                        "Set Item Color",
                        "Set one active Item PLT color through Smalls policy",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.item.set_color <part-id> <color-id> <palette-value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto part = parse_i32(command_arg_string(invocation.args, 0));
            const auto color = parse_i32(command_arg_string(invocation.args, 1));
            const auto value = parse_i32(command_arg_string(invocation.args, 2));
            if (!part || !color || !value || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.item.set_color <part-id> <color-id> <palette-value>",
                    CommandOutputChannel::warn);
            }
            const ObjectHandle object = bridge_->active_object();
            const std::array parts{*part};
            const std::array colors{*color};
            const std::array values{*value};
            auto batch = make_item_color_edits(
                kernel::runtime(), object, parts, colors, values);
            if (!batch) {
                return command_result(CommandStatus::rejected,
                    "Active Item does not expose that color row",
                    CommandOutputChannel::warn);
            }
            if (batch->patches.front().before == *value) {
                return command_result(CommandStatus::noop,
                    "Item color is already set", CommandOutputChannel::none);
            }
            return commit_object_edits(
                std::move(*batch), "Set Item color", context);
        });

    register_or_log(CommandSpec{
                        "object.item.add_property",
                        "Add Item Property",
                        "Add one valid property to the active Item",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.item.add_property <type> <subtype> <cost-table> <cost-value> <param-table> <param-value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto prop_type = parse_i32(command_arg_string(invocation.args, 0));
            const auto subtype = parse_i32(command_arg_string(invocation.args, 1));
            const auto cost_table = parse_i32(command_arg_string(invocation.args, 2));
            const auto cost_value = parse_i32(command_arg_string(invocation.args, 3));
            const auto param_table = parse_i32(command_arg_string(invocation.args, 4));
            const auto param_value = parse_i32(command_arg_string(invocation.args, 5));
            if (!prop_type || !subtype || !cost_table || !cost_value
                || !param_table || !param_value || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.item.add_property <type> <subtype> <cost-table> <cost-value> <param-table> <param-value>",
                    CommandOutputChannel::warn);
            }
            const ItemPropertyRecord record{
                .prop_type = *prop_type,
                .subtype = *subtype,
                .cost_table = *cost_table,
                .cost_value = *cost_value,
                .param_table = *param_table,
                .param_value = *param_value,
            };
            const ObjectHandle object = bridge_->active_object();
            const auto properties = snapshot_item_property_records(
                kernel::runtime(), object);
            if (!properties) {
                return command_result(CommandStatus::rejected,
                    "Active object is not an Item",
                    CommandOutputChannel::warn);
            }
            ItemPropertyEditBatch batch{
                .item = object,
                .kind = ItemPropertyEditKind::insert,
            };
            batch.rows.push_back({
                .index = static_cast<int32_t>(properties->size()),
                .after = record,
            });
            return commit_item_property_edits(
                std::move(batch), "Add Item property", context);
        });

    register_or_log(CommandSpec{
                        "object.item.remove_property",
                        "Remove Item Property",
                        "Remove one property from the active Item",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.item.remove_property <property-index>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto index = parse_i32(command_arg_string(invocation.args, 0));
            if (!index || *index < 0 || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.item.remove_property <property-index>",
                    CommandOutputChannel::warn);
            }
            const ObjectHandle object = bridge_->active_object();
            const auto properties = snapshot_item_property_records(
                kernel::runtime(), object);
            if (!properties || static_cast<size_t>(*index) >= properties->size()) {
                return command_result(CommandStatus::rejected,
                    "Item property index is unavailable",
                    CommandOutputChannel::warn);
            }
            ItemPropertyEditBatch batch{
                .item = object,
                .kind = ItemPropertyEditKind::remove,
            };
            batch.rows.push_back({
                .index = *index,
                .before = (*properties)[static_cast<size_t>(*index)],
            });
            return commit_item_property_edits(
                std::move(batch), "Remove Item property", context);
        });

    register_or_log(CommandSpec{
                        "object.item.set_property_value",
                        "Set Item Property Value",
                        "Set one subtype, parameter, or cost option on an active Item property",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.item.set_property_value <property-index> <subtype|param|cost> <value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto index = parse_i32(command_arg_string(invocation.args, 0));
            const std::string field_name = command_arg_string(invocation.args, 1);
            const auto value = parse_i32(command_arg_string(invocation.args, 2));
            int32_t field = -1;
            if (field_name == "subtype") { field = 0; }
            if (field_name == "param") { field = 1; }
            if (field_name == "cost") { field = 2; }
            if (!index || !value || *index < 0 || field < 0 || !bridge_) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.item.set_property_value <property-index> <subtype|param|cost> <value>",
                    CommandOutputChannel::warn);
            }
            const ObjectHandle object = bridge_->active_object();
            const auto properties = snapshot_item_property_records(
                kernel::runtime(), object);
            if (!properties || static_cast<size_t>(*index) >= properties->size()) {
                return command_result(CommandStatus::rejected,
                    "Item property index is unavailable",
                    CommandOutputChannel::warn);
            }
            ItemPropertyEditRow row{
                .index = *index,
                .field = field,
                .before = (*properties)[static_cast<size_t>(*index)],
            };
            row.after = row.before;
            if (field == 0) { row.after.subtype = *value; }
            if (field == 1) { row.after.param_value = *value; }
            if (field == 2) { row.after.cost_value = *value; }
            if (row.before == row.after) {
                return command_result(CommandStatus::noop,
                    "Item property value is already set", CommandOutputChannel::none);
            }
            ItemPropertyEditBatch batch{
                .item = object,
                .kind = ItemPropertyEditKind::value,
                .rows = {row},
            };
            return commit_item_property_edits(
                std::move(batch), "Set Item property value", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.set_body_part",
                        "Set Creature Body Part",
                        "Set one dynamic Creature body-part model value",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.set_body_part <part-id> <model-value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto part = parse_u32(command_arg_string(invocation.args, 0));
            const auto value = parse_u32(command_arg_string(invocation.args, 1));
            if (!part || !value
                || *value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.set_body_part <part-id> <model-value>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable",
                    CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::creature) {
                return command_result(CommandStatus::rejected,
                    "Active object is not a Creature",
                    CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = editable_creature_body_parts(runtime, object);
            if (current.empty()) {
                return command_result(CommandStatus::rejected,
                    "Active Creature appearance has no editable body parts",
                    CommandOutputChannel::warn);
            }
            if (*part >= current.size()) {
                return command_result(CommandStatus::rejected,
                    "Creature body-part id is outside the active Smalls body-part row",
                    CommandOutputChannel::warn);
            }

            const int32_t desired = static_cast<int32_t>(*value);
            if (current[*part] == desired) {
                return command_result(CommandStatus::noop,
                    "Creature body part is already set",
                    CommandOutputChannel::none);
            }

            ObjectEditBatch batch;
            batch.kind = ObjectEditKind::creature_body_part;
            batch.patches.push_back({object, {}, *part, current[*part], desired});
            return commit_object_edits(
                std::move(batch), "Set Creature body part", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.set_color",
                        "Set Creature Color",
                        "Set one dynamic Creature PLT color",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.set_color <color-id> <palette-value>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto color = parse_u32(command_arg_string(invocation.args, 0));
            const auto value = parse_u32(command_arg_string(invocation.args, 1));
            if (!color || !value
                || *value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.set_color <color-id> <palette-value>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable",
                    CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::creature) {
                return command_result(CommandStatus::rejected,
                    "Active object is not a Creature",
                    CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = editable_creature_colors(runtime, object);
            if (current.empty()) {
                return command_result(CommandStatus::rejected,
                    "Active Creature appearance has no editable PLT colors",
                    CommandOutputChannel::warn);
            }
            if (*color >= current.size()) {
                return command_result(CommandStatus::rejected,
                    "Creature color id is outside the active Smalls color row",
                    CommandOutputChannel::warn);
            }

            const int32_t desired = static_cast<int32_t>(*value);
            if (current[*color] == desired) {
                return command_result(CommandStatus::noop,
                    "Creature color is already set",
                    CommandOutputChannel::none);
            }

            ObjectEditBatch batch;
            batch.kind = ObjectEditKind::creature_color;
            batch.patches.push_back({object, {}, *color, current[*color], desired});
            return commit_object_edits(
                std::move(batch), "Set Creature color", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.set_accessory",
                        "Set Creature Accessory",
                        "Set the active Creature wings or tail",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.set_accessory <wings|tail> <model-row>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const std::string_view accessory_name = command_arg_string(invocation.args, 0);
            uint32_t accessory = 0;
            if (accessory_name == "wings") {
                accessory = 0;
            } else if (accessory_name == "tail") {
                accessory = 1;
            } else {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.set_accessory <wings|tail> <model-row>",
                    CommandOutputChannel::warn);
            }
            const auto value = parse_u32(command_arg_string(invocation.args, 1));
            if (!value || *value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.set_accessory <wings|tail> <model-row>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable",
                    CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::creature) {
                return command_result(CommandStatus::rejected,
                    "Active object is not a Creature",
                    CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = editable_creature_accessories(runtime, object);
            if (accessory >= current.size()) {
                return command_result(CommandStatus::rejected,
                    "Active Creature has no editable accessory state",
                    CommandOutputChannel::warn);
            }

            const int32_t desired = static_cast<int32_t>(*value);
            if (current[accessory] == desired) {
                return command_result(CommandStatus::noop,
                    "Creature accessory is already set",
                    CommandOutputChannel::none);
            }

            ObjectEditBatch batch;
            batch.kind = ObjectEditKind::creature_accessory;
            batch.patches.push_back({object, {}, accessory, current[accessory], desired});
            return commit_object_edits(
                std::move(batch), "Set Creature accessory", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.adjust_class_level",
                        "Adjust Creature Class Level",
                        "Increase or decrease one active Creature class level through Smalls policy",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.adjust_class_level <class-slot> <-1|1>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto slot = parse_u32(command_arg_string(invocation.args, 0));
            const auto delta = parse_i32(command_arg_string(invocation.args, 1));
            if (!slot || *slot >= 8 || !delta || (*delta != -1 && *delta != 1)) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.adjust_class_level <class-slot> <-1|1>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::creature) {
                return command_result(CommandStatus::rejected,
                    "Active object is not a Creature", CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = editable_creature_class_levels(runtime, object);
            if (current.size() != 8 || current[*slot] <= 0) {
                return command_result(CommandStatus::rejected,
                    "Creature class slot is unavailable", CommandOutputChannel::warn);
            }
            const int64_t desired = static_cast<int64_t>(current[*slot]) + *delta;
            if (desired < std::numeric_limits<int32_t>::min()
                || desired > std::numeric_limits<int32_t>::max()) {
                return command_result(CommandStatus::rejected,
                    "Creature class level is outside its valid range", CommandOutputChannel::warn);
            }

            ObjectEditBatch batch;
            batch.kind = ObjectEditKind::creature_class_level;
            batch.patches.push_back({object, {}, *slot, current[*slot], static_cast<int32_t>(desired)});
            return commit_object_edits(
                std::move(batch), "Adjust Creature class level", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.set_feat",
                        "Set Creature Feat",
                        "Assign or remove a feat on the active Creature",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.set_feat <feat-id> <0|1>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto feat_id = parse_u32(command_arg_string(invocation.args, 0));
            const auto assigned = parse_assignment(command_arg_string(invocation.args, 1));
            if (!feat_id || !assigned) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.set_feat <feat-id> <0|1>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed, "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            const ObjectHandle object = bridge_->active_object();
            if (object.type != ObjectType::creature) {
                return command_result(CommandStatus::rejected, "Active object is not a Creature", CommandOutputChannel::warn);
            }

            auto& runtime = kernel::runtime();
            const auto current = creature_has_feat(runtime, object, *feat_id);
            if (!current) {
                return command_result(CommandStatus::rejected, "Feat assignment is unavailable", CommandOutputChannel::warn);
            }
            if (*current == *assigned) {
                return command_result(CommandStatus::noop, "Creature feat assignment is already set", CommandOutputChannel::none);
            }

            ObjectEditBatch batch;
            batch.kind = ObjectEditKind::creature_feat;
            batch.patches.push_back({object, {}, *feat_id, *current ? 1 : 0, *assigned ? 1 : 0});
            return commit_object_edits(std::move(batch), "Set Creature feat", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.set_known_spell",
                        "Set Creature Known Spell",
                        "Assign or remove one known spell through Smalls policy",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.set_known_spell <class-id> <spell-id> <0|1>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto class_id = parse_u32(command_arg_string(invocation.args, 0));
            const auto spell_id = parse_u32(command_arg_string(invocation.args, 1));
            const auto known = parse_assignment(command_arg_string(invocation.args, 2));
            if (!class_id || !spell_id || !known
                || *class_id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                || *spell_id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.set_known_spell <class-id> <spell-id> <0|1>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            auto edit = make_creature_known_spell_edit(kernel::runtime(),
                bridge_->active_object(), static_cast<int32_t>(*class_id),
                static_cast<int32_t>(*spell_id), *known);
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Known spell is invalid, unavailable, or already set",
                    CommandOutputChannel::warn);
            }
            return commit_creature_spell_edits(
                std::move(*edit), "Set Creature known spell", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.adjust_memorized_spell",
                        "Adjust Creature Memorized Spell",
                        "Add or remove one memorized spell use through Smalls policy",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.adjust_memorized_spell <class-id> <spell-id> <metamagic> <-1|1>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto class_id = parse_u32(command_arg_string(invocation.args, 0));
            const auto spell_id = parse_u32(command_arg_string(invocation.args, 1));
            const auto metamagic = parse_u32(command_arg_string(invocation.args, 2));
            const auto delta = parse_i32(command_arg_string(invocation.args, 3));
            if (!class_id || !spell_id || !metamagic || !delta
                || *class_id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                || *spell_id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                || *metamagic > 255 || (*delta != -1 && *delta != 1)) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.adjust_memorized_spell <class-id> <spell-id> <metamagic> <-1|1>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            auto edit = make_creature_memorized_spell_edit(kernel::runtime(),
                bridge_->active_object(), static_cast<int32_t>(*class_id),
                static_cast<int32_t>(*spell_id), static_cast<int32_t>(*metamagic), *delta);
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Memorized spell change is invalid or unavailable",
                    CommandOutputChannel::warn);
            }
            return commit_creature_spell_edits(
                std::move(*edit), "Adjust Creature memorized spell", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.equip_inventory_item",
                        "Equip Creature Inventory Item",
                        "Equip one inventory row into an empty Creature slot",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.equip_inventory_item <inventory-index> <slot-index>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto inventory_index = parse_u32(command_arg_string(invocation.args, 0));
            const auto slot_index = parse_u32(command_arg_string(invocation.args, 1));
            if (!inventory_index || !slot_index || *slot_index >= 18) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.equip_inventory_item <inventory-index> <slot-index>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            auto edit = make_creature_inventory_equip_edit(
                bridge_->active_object(), *inventory_index, static_cast<EquipIndex>(*slot_index));
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Inventory row is stale, invalid, or the equipment slot is occupied",
                    CommandOutputChannel::warn);
            }
            return commit_creature_inventory_edits(
                std::move(*edit), "Equip Creature item", context);
        });

    register_or_log(CommandSpec{
                        "object.creature.unequip_slot",
                        "Unequip Creature Slot",
                        "Move one equipped Creature item into inventory",
                        "object",
                        {},
                        CommandScope::workspace,
                        CommandFlags::none,
                        {},
                        "object.creature.unequip_slot <slot-index>",
                    },
        [this](const CommandInvocation& invocation, CommandContext& context) {
            const auto slot_index = parse_u32(command_arg_string(invocation.args, 0));
            if (!slot_index || *slot_index >= 18) {
                return command_result(CommandStatus::rejected,
                    "Usage: object.creature.unequip_slot <slot-index>",
                    CommandOutputChannel::warn);
            }
            if (!bridge_) {
                return command_result(CommandStatus::failed,
                    "Smalls bridge unavailable", CommandOutputChannel::error);
            }

            auto edit = make_creature_inventory_unequip_edit(
                bridge_->active_object(), static_cast<EquipIndex>(*slot_index));
            if (!edit) {
                return command_result(CommandStatus::rejected,
                    "Equipment slot is empty, invalid, or stale",
                    CommandOutputChannel::warn);
            }
            return commit_creature_inventory_edits(
                std::move(*edit), "Unequip Creature item", context);
        });

    register_or_log(CommandSpec{
                        "toolset.save_all",
                        "Save All",
                        "Save all dirty documents",
                        "workspace",
                        {"saveall"},
                        CommandScope::global,
                        CommandFlags::none,
                        "Ctrl+Shift+S",
                        "toolset.save_all",
                    },
        [](const CommandInvocation&, CommandContext&) {
            return command_result(CommandStatus::noop, "Command: Save All (stub)");
        });

    register_or_log(CommandSpec{
                        "toolset.open_recent",
                        "Open Recent",
                        "Show recent projects",
                        "workspace",
                        {"recent"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "toolset.open_recent",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!workspace_) {
                return command_result(CommandStatus::failed, "Workspace unavailable", CommandOutputChannel::error);
            }
            const std::string title = current_project_dir_.empty()
                ? std::string{"Home"}
                : project_display_name(current_project_dir_);
            workspace_->ensure_default_tabs(title);
            return command_result(CommandStatus::success, "Opened Home", CommandOutputChannel::none);
        });

    register_or_log(CommandSpec{
                        "toolset.module_properties",
                        "Module Properties",
                        "Open module properties",
                        "module",
                        {"props"},
                        CommandScope::global,
                        CommandFlags::none,
                        {},
                        "toolset.module_properties",
                    },
        [this](const CommandInvocation&, CommandContext&) {
            if (!bridge_ || !workspace_) {
                return command_result(CommandStatus::failed,
                    "Workspace unavailable", CommandOutputChannel::error);
            }

            const ObjectHandle object = module_object();
            const auto* module = nw::kernel::objects().get<nw::Module>(object);
            if (!module) {
                return command_result(CommandStatus::rejected,
                    "No module is open", CommandOutputChannel::warn);
            }

            std::string title = nw::kernel::strings().get(module->name);
            if (is_blank_ascii(title)) {
                title = current_project_dir_.empty()
                    ? std::string{"Module"}
                    : project_display_name(current_project_dir_);
            }
            workspace_->ensure_default_tabs(std::move(title));
            bridge_->publish_active_object(object);
            return command_result(CommandStatus::success,
                "Opened Module Details", CommandOutputChannel::none);
        });
}

std::vector<RecentModuleEntry> ToolsetBackend::list_modules(std::string_view query, size_t limit) const
{
    if (!bridge_) {
        return {};
    }

    namespace fs = std::filesystem;

    struct ModulePath {
        fs::path path;
        fs::file_time_type mtime{};
    };

    std::vector<ModulePath> candidates;

    std::vector<fs::path> roots;
    roots.reserve(6);

    const auto& cfg_install = nw::kernel::config().install_path();
    const auto& cfg_user = nw::kernel::config().user_path();
    if (!cfg_install.empty()) {
        roots.push_back(cfg_install / "modules");
    }
    if (!cfg_user.empty()) {
        roots.push_back(cfg_user / "modules");
    }

    if (auto env_root = std::getenv("NWN_ROOT")) {
        if (*env_root) {
            roots.push_back(fs::path(env_root) / "modules");
        }
    }
    if (auto env_home = std::getenv("NWN_HOME")) {
        if (*env_home) {
            roots.push_back(fs::path(env_home) / "modules");
        }
    }

    const auto detected = nw::probe_nwn_install(nw::kernel::config().version());
    if (!detected.install.empty()) {
        roots.push_back(detected.install / "modules");
    }
    if (!detected.user.empty()) {
        roots.push_back(detected.user / "modules");
    }

    std::unordered_set<std::string> seen_roots;
    seen_roots.reserve(roots.size());
    std::vector<fs::path> unique_roots;
    unique_roots.reserve(roots.size());
    for (const auto& root : roots) {
        if (root.empty()) {
            continue;
        }
        std::error_code ec;
        const auto normalized = fs::weakly_canonical(root, ec);
        const fs::path key_path = ec ? root.lexically_normal() : normalized;
        const std::string key = key_path.string();
        if (key.empty() || !seen_roots.insert(key).second) {
            continue;
        }
        unique_roots.push_back(key_path);
    }

    for (const auto& root : unique_roots) {
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
            continue;
        }

        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (ec || !is_module_container(entry)) {
                continue;
            }

            auto modified = fs::file_time_type::min();
            modified = entry.last_write_time(ec);
            candidates.push_back({entry.path(), modified});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const ModulePath& lhs, const ModulePath& rhs) {
        return lhs.mtime > rhs.mtime;
    });

    if (limit > 0 && candidates.size() > limit) {
        candidates.resize(limit);
    }

    std::vector<RecentModuleEntry> modules;
    modules.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        modules.push_back({candidate.path.stem().string(), candidate.path.string()});
    }

    if (query.empty()) {
        return modules;
    }

    modules.erase(std::remove_if(modules.begin(), modules.end(), [query](const RecentModuleEntry& mod) {
        return !contains_casefold(mod.name, query) && !contains_casefold(mod.path, query);
    }),
        modules.end());
    return modules;
}

std::vector<CommandSpec> ToolsetBackend::list_commands(std::string_view query) const
{
    auto commands = command_bus_.list_commands();
    if (query.empty()) {
        return commands;
    }

    commands.erase(std::remove_if(commands.begin(), commands.end(), [query](const CommandSpec& cmd) {
        if (contains_casefold(cmd.id, query)
            || contains_casefold(cmd.title, query)
            || contains_casefold(cmd.description, query)
            || contains_casefold(cmd.usage, query)
            || contains_casefold(cmd.category, query)) {
            return false;
        }

        for (const auto& alias : cmd.aliases) {
            if (contains_casefold(alias, query)) {
                return false;
            }
        }
        return true;
    }),
        commands.end());
    return commands;
}

std::vector<LoadedAreaEntry> ToolsetBackend::list_areas(std::string_view query) const
{
    if (!bridge_) {
        return {};
    }

    auto areas = loaded_areas_;
    if (!query.empty()) {
        areas.erase(std::remove_if(areas.begin(), areas.end(), [query](const LoadedAreaEntry& area) {
            return !contains_casefold(area.name, query) && !contains_casefold(area.resref, query);
        }),
            areas.end());
    }

    std::sort(areas.begin(), areas.end(), [](const LoadedAreaEntry& lhs, const LoadedAreaEntry& rhs) {
        const std::string lhs_name = to_lower_ascii(display_name_or_resref(lhs));
        const std::string rhs_name = to_lower_ascii(display_name_or_resref(rhs));
        if (lhs_name != rhs_name) {
            return lhs_name < rhs_name;
        }

        const std::string lhs_resref = to_lower_ascii(lhs.resref);
        const std::string rhs_resref = to_lower_ascii(rhs.resref);
        if (lhs_resref != rhs_resref) {
            return lhs_resref < rhs_resref;
        }

        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.resref < rhs.resref;
    });
    return areas;
}

ProjectTreeResult ToolsetBackend::list_project_tree(std::string_view query) const
{
    if (current_project_dir_.empty()) {
        ProjectTreeResult result;
        result.message = "No project is open";
        return result;
    }
    return load_project_tree(current_project_dir_, query);
}

ProjectModuleSummary ToolsetBackend::project_module_summary() const
{
    if (current_project_dir_.empty()) {
        ProjectModuleSummary result;
        result.message = "No project is open";
        return result;
    }
    return load_project_module_summary(current_project_dir_);
}

bool ToolsetBackend::has_command(std::string_view command_id) const
{
    return command_bus_.has_command(command_id);
}

std::filesystem::path ToolsetBackend::current_project_dir() const
{
    return current_project_dir_;
}

ObjectHandle ToolsetBackend::module_object() const noexcept
{
    const auto* objects = nw::kernel::services().get<nw::ObjectManager>();
    return objects && objects->valid(module_object_) ? module_object_ : ObjectHandle{};
}

uint64_t ToolsetBackend::module_generation() const noexcept
{
    return module_generation_;
}

CommandResult ToolsetBackend::open_module(std::string_view module_path)
{
    if (!bridge_) {
        return command_result(CommandStatus::failed, "Backend unavailable", CommandOutputChannel::error);
    }
    if (module_path.empty()) {
        return command_result(CommandStatus::rejected, "No module path provided", CommandOutputChannel::warn);
    }

    try {
        const auto start = std::chrono::steady_clock::now();
        const auto path = std::filesystem::path(module_path);
        module_object_ = ObjectHandle{};
        current_project_dir_.clear();
        auto* module = nw::kernel::load_module(path, true);
        if (!module) {
            return command_result(CommandStatus::failed,
                std::string{"Failed to open module: "} + std::string(module_path),
                CommandOutputChannel::error);
        }

        ++module_generation_;
        module_object_ = module->handle();

        loaded_areas_.clear();
        loaded_areas_.reserve(module->area_count());
        for (size_t i = 0; i < module->area_count(); ++i) {
            if (const auto* area = module->get_area(i)) {
                auto display_name = pick_locstring_name(area->name);
                if (is_blank_ascii(display_name)) {
                    display_name = pick_locstring_name(area->ObjectBase::name);
                }
                if (is_blank_ascii(display_name)) {
                    display_name = sanitize_label(nw::kernel::strings().get(area->name));
                }
                if (is_blank_ascii(display_name)) {
                    display_name = sanitize_label(nw::kernel::strings().get(area->ObjectBase::name));
                }
                if (is_blank_ascii(display_name)) {
                    display_name = std::string(area->resref.view());
                }
                loaded_areas_.push_back({
                    .name = std::move(display_name),
                    .resref = std::string(area->resref.view()),
                });
            }
        }

        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (shell_) {
            shell_->set_showing_areas(false);
            shell_->set_showing_project_tree(false);
        }
        if (workspace_) {
            std::string title = nw::kernel::strings().get(module->name);
            if (is_blank_ascii(title)) {
                title = path.stem().string();
            }
            workspace_->ensure_default_tabs(std::move(title), true);
        }
        return command_result(CommandStatus::success,
            std::string{"Loaded module in "} + std::to_string(elapsed_ms) + " ms: " + std::string(module_path));
    } catch (...) {
        return command_result(CommandStatus::failed,
            std::string{"Exception opening module: "} + std::string(module_path),
            CommandOutputChannel::error);
    }
}

CommandResult ToolsetBackend::open_project(std::string_view project_path)
{
    if (!bridge_) {
        return command_result(CommandStatus::failed, "Backend unavailable", CommandOutputChannel::error);
    }
    if (project_path.empty()) {
        return command_result(CommandStatus::rejected, "No project path provided", CommandOutputChannel::warn);
    }

    namespace fs = std::filesystem;
    try {
        const fs::path path{project_path};
        if (!is_project_directory(path)) {
            return command_result(CommandStatus::failed,
                std::string{"Not an Client project: "} + std::string(project_path),
                CommandOutputChannel::error);
        }

        std::error_code ec;
        const auto canonical = fs::weakly_canonical(path, ec);
        const auto project_dir = ec ? path.lexically_normal() : canonical;

        const auto tree = load_project_tree(project_dir);
        if (!tree.ok) {
            return command_result(CommandStatus::failed, tree.message, CommandOutputChannel::error);
        }
        const auto load_options = nw::kernel::module_load_options_for_project(project_dir);
        module_object_ = ObjectHandle{};
        current_project_dir_.clear();
        auto* module = nw::kernel::load_module(project_dir, false, load_options);
        if (!module) {
            return command_result(CommandStatus::failed,
                "Failed to load project module: " + project_dir.string(),
                CommandOutputChannel::error);
        }

        ++module_generation_;
        module_object_ = module->handle();

        current_project_dir_ = project_dir;
        loaded_areas_.clear();
        append_project_areas(tree.root, current_project_dir_, loaded_areas_);

        if (shell_) {
            shell_->set_showing_project_tree(true);
        }
        if (workspace_) {
            workspace_->ensure_default_tabs(project_display_name(current_project_dir_), true);
        }
        return command_result(CommandStatus::success,
            "Opened project: " + current_project_dir_.string()
                + " (" + std::to_string(tree.node_count) + " entries)");
    } catch (const std::exception& e) {
        return command_result(CommandStatus::failed,
            std::string{"Exception opening project: "} + e.what(),
            CommandOutputChannel::error);
    }
}

CommandResult ToolsetBackend::execute_command(std::string_view command_id, const std::vector<std::string_view>& args, CommandContext context)
{
    CommandArgs command_args;
    command_args.reserve(args.size());
    for (std::string_view arg : args) {
        command_args.push_back(CommandArg::positional_string(std::string(arg)));
    }
    return command_bus_.execute(command_id, std::move(command_args), context_with_backend_defaults(std::move(context), workspace_));
}

CommandResult ToolsetBackend::execute_command(CommandInvocation invocation, CommandContext context)
{
    return command_bus_.execute(std::move(invocation), context_with_backend_defaults(std::move(context), workspace_));
}

CommandResult ToolsetBackend::place_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    CommandContext context)
{
    context = context_with_backend_defaults(std::move(context), workspace_);
    CommandResult result = nw::toolset::place_area_objects(
        area, objects, "Place area object", context);
    if (result.ok() && result.undo_action && context.record_undo && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

CommandResult ToolsetBackend::place_creature_items(
    ObjectHandle creature,
    std::span<const ItemPlacement> placements,
    CommandContext context)
{
    context = context_with_backend_defaults(std::move(context), workspace_);
    CommandResult result = nw::toolset::place_creature_items(
        creature, placements, "Place Creature item", context);
    if (result.ok() && result.undo_action && context.record_undo && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

CommandResult ToolsetBackend::place_items(
    ObjectHandle owner,
    std::span<const ItemPlacement> placements,
    CommandContext context)
{
    context = context_with_backend_defaults(std::move(context), workspace_);
    CommandResult result = nw::toolset::place_items(
        owner, placements, "Place item", context);
    if (result.ok() && result.undo_action && context.record_undo && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

CommandResult ToolsetBackend::place_store_items(
    ObjectHandle store,
    std::span<const StoreItemPlacement> placements,
    CommandContext context)
{
    context = context_with_backend_defaults(std::move(context), workspace_);
    CommandResult result = nw::toolset::place_store_items(
        store, placements, "Place Store item", context);
    if (result.ok() && result.undo_action && context.record_undo && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

CommandResult ToolsetBackend::replace_encounter_spawns(
    EncounterSpawnEdit edit, CommandContext context)
{
    context = context_with_backend_defaults(std::move(context), workspace_);
    CommandResult result = nw::toolset::commit_encounter_spawn_edit(
        std::move(edit), "Add encounter spawn", context);
    if (result.ok() && result.undo_action && context.record_undo && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

CommandResult ToolsetBackend::replace_sound_resources(
    SoundResourceEdit edit, CommandContext context)
{
    context = context_with_backend_defaults(std::move(context), workspace_);
    CommandResult result = nw::toolset::commit_sound_resource_edit(
        std::move(edit), "Add sound resource", context);
    if (result.ok() && result.undo_action && context.record_undo && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

TerminalCompletionResult ToolsetBackend::complete_console_command(std::string_view line, size_t cursor_byte_position) const
{
    return terminal_.complete(command_bus_, line, cursor_byte_position);
}

bool ToolsetBackend::is_open_module_dialog_invocation(std::string_view line) const
{
    const TerminalParseResult parsed = terminal_.parse(line);
    if (!parsed.error.empty() || parsed.empty || !parsed.invocation.args.empty()) {
        return false;
    }
    return command_bus_.resolve_id(parsed.invocation.command_id) == "toolset.open";
}

bool ToolsetBackend::is_open_project_dialog_invocation(std::string_view line) const
{
    const TerminalParseResult parsed = terminal_.parse(line);
    if (!parsed.error.empty() || parsed.empty || !parsed.invocation.args.empty()) {
        return false;
    }
    return command_bus_.resolve_id(parsed.invocation.command_id) == "toolset.open_project";
}

CommandResult ToolsetBackend::console_execute(std::string_view line, CommandContext context)
{
    return terminal_.execute(command_bus_, line, context_with_backend_defaults(std::move(context), workspace_));
}

} // namespace nw::toolset
