#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace nw::toolset {

class WorkspaceState;

struct CommandResourceRef {
    std::string type;
    std::string resref;
};

struct CommandObjectId {
    uint64_t value = 0;
    std::string type;
};

using CommandValue = std::variant<std::monostate, std::string, int64_t, bool, CommandResourceRef, CommandObjectId>;

struct CommandArg {
    std::string name;
    CommandValue value;

    [[nodiscard]] static CommandArg positional_string(std::string value);
    [[nodiscard]] static CommandArg named_string(std::string name, std::string value);
};

using CommandArgs = std::vector<CommandArg>;

enum class CommandScope : uint8_t {
    global,
    workspace,
    renderer,
};

enum class CommandSource : uint8_t {
    palette,
    terminal,
    menu,
    shortcut,
    widget,
    renderer,
    script,
};

enum class CommandStatus : uint8_t {
    success,
    failed,
    unknown_command,
    rejected,
    noop,
};

enum class CommandOutputChannel : uint8_t {
    none,
    info,
    warn,
    error,
    script,
};

enum class CommandFlags : uint32_t {
    none = 0,
    hidden = 1u << 0u,
    disabled = 1u << 1u,
    script = 1u << 2u,
};

[[nodiscard]] constexpr CommandFlags operator|(CommandFlags lhs, CommandFlags rhs) noexcept
{
    return static_cast<CommandFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

[[nodiscard]] constexpr CommandFlags operator&(CommandFlags lhs, CommandFlags rhs) noexcept
{
    return static_cast<CommandFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_flag(CommandFlags flags, CommandFlags flag) noexcept
{
    return (static_cast<uint32_t>(flags & flag) != 0u);
}

struct CommandSpec {
    std::string id;
    std::string title;
    std::string description;
    std::string category;
    std::vector<std::string> aliases;
    CommandScope scope = CommandScope::global;
    CommandFlags flags = CommandFlags::none;
    std::string default_binding;
    std::string usage;
};

struct CommandContext {
    std::string active_tab_id;
    std::vector<std::string> selection;
    std::string focused_region;
    nw::ObjectHandle area_object{};
    CommandSource source = CommandSource::palette;
    WorkspaceState* workspace = nullptr;
    bool record_undo = true;
};

struct CommandInvocation {
    std::string command_id;
    CommandArgs args;
};

struct CommandUndoAction;

struct CommandPromptAction {
    std::string id;
    std::string label;
    std::string command_id;
    std::vector<std::string> args;
};

struct CommandPrompt {
    std::string id;
    std::string title;
    std::string message;
    std::string detail;
    std::vector<CommandPromptAction> actions;
};

struct CommandResult {
    CommandStatus status = CommandStatus::success;
    std::string message;
    CommandOutputChannel output_channel = CommandOutputChannel::info;
    std::shared_ptr<CommandUndoAction> undo_action;
    std::optional<CommandPrompt> prompt;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] bool should_log() const noexcept;
};

struct CommandUndoAction {
    std::string label;
    std::function<CommandResult(CommandContext&)> undo;
    std::function<CommandResult(CommandContext&)> redo;
};

class CommandBus {
public:
    using Handler = std::function<CommandResult(const CommandInvocation&, CommandContext&)>;

    [[nodiscard]] bool register_command(CommandSpec spec, Handler handler, std::string* error = nullptr);
    [[nodiscard]] std::vector<CommandSpec> list_commands(bool include_hidden = false) const;
    [[nodiscard]] const CommandSpec* find_spec(std::string_view id_or_alias) const;
    [[nodiscard]] bool has_command(std::string_view id_or_alias) const;
    [[nodiscard]] std::string resolve_id(std::string_view id_or_alias) const;

    CommandResult execute(CommandInvocation invocation, CommandContext context);
    CommandResult execute(std::string_view id_or_alias, CommandArgs args, CommandContext context);

private:
    struct CommandEntry {
        CommandSpec spec;
        Handler handler;
    };

    [[nodiscard]] const CommandEntry* find_entry(std::string_view id_or_alias) const;
    [[nodiscard]] CommandEntry* find_entry(std::string_view id_or_alias);

    std::vector<CommandEntry> entries_;
    std::unordered_map<std::string, size_t> id_to_index_;
    std::unordered_map<std::string, std::string> alias_to_id_;
};

[[nodiscard]] std::string_view command_status_name(CommandStatus status) noexcept;
[[nodiscard]] std::string_view command_output_channel_name(CommandOutputChannel channel) noexcept;
[[nodiscard]] std::string command_arg_string(const CommandArg& arg);
[[nodiscard]] std::string command_arg_string(const CommandArgs& args, size_t index);

} // namespace nw::toolset
