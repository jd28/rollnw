#include "command_bus.hpp"

#include "workspace.hpp"

#include <algorithm>
#include <utility>

namespace nw::toolset {

namespace {

bool is_success_for_undo(CommandStatus status) noexcept
{
    return status == CommandStatus::success;
}

CommandResult make_result(CommandStatus status, std::string message, CommandOutputChannel channel)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = channel;
    return result;
}

bool validate_unique_text(std::string_view value, std::string_view kind, std::string* error)
{
    if (!value.empty()) {
        return true;
    }
    if (error) {
        *error = std::string(kind) + " is empty";
    }
    return false;
}

} // namespace

CommandArg CommandArg::positional_string(std::string value)
{
    CommandArg arg;
    arg.value = std::move(value);
    return arg;
}

CommandArg CommandArg::named_string(std::string name, std::string value)
{
    CommandArg arg;
    arg.name = std::move(name);
    arg.value = std::move(value);
    return arg;
}

bool CommandResult::ok() const noexcept
{
    return status == CommandStatus::success || status == CommandStatus::noop;
}

bool CommandResult::should_log() const noexcept
{
    return output_channel != CommandOutputChannel::none && !message.empty();
}

bool CommandBus::register_command(CommandSpec spec, Handler handler, std::string* error)
{
    if (!validate_unique_text(spec.id, "command id", error)) {
        return false;
    }
    if (!handler) {
        if (error) {
            *error = "command handler is empty";
        }
        return false;
    }
    if (id_to_index_.find(spec.id) != id_to_index_.end()) {
        if (error) {
            *error = std::string{"duplicate command id: "} + spec.id;
        }
        return false;
    }
    if (alias_to_id_.find(spec.id) != alias_to_id_.end()) {
        if (error) {
            *error = std::string{"command id conflicts with alias: "} + spec.id;
        }
        return false;
    }

    for (const auto& alias : spec.aliases) {
        if (alias.empty()) {
            if (error) {
                *error = std::string{"empty alias for command: "} + spec.id;
            }
            return false;
        }
        if (id_to_index_.find(alias) != id_to_index_.end() || alias_to_id_.find(alias) != alias_to_id_.end()) {
            if (error) {
                *error = std::string{"duplicate command alias: "} + alias;
            }
            return false;
        }
    }

    if (spec.usage.empty()) {
        spec.usage = spec.id;
    }

    const size_t index = entries_.size();
    for (const auto& alias : spec.aliases) {
        alias_to_id_[alias] = spec.id;
    }
    id_to_index_[spec.id] = index;
    entries_.push_back(CommandEntry{std::move(spec), std::move(handler)});
    return true;
}

std::vector<CommandSpec> CommandBus::list_commands(bool include_hidden) const
{
    std::vector<CommandSpec> out;
    out.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (!include_hidden && has_flag(entry.spec.flags, CommandFlags::hidden)) {
            continue;
        }
        out.push_back(entry.spec);
    }
    return out;
}

const CommandSpec* CommandBus::find_spec(std::string_view id_or_alias) const
{
    if (const auto* entry = find_entry(id_or_alias)) {
        return &entry->spec;
    }
    return nullptr;
}

bool CommandBus::has_command(std::string_view id_or_alias) const
{
    return find_entry(id_or_alias) != nullptr;
}

std::string CommandBus::resolve_id(std::string_view id_or_alias) const
{
    if (auto it = id_to_index_.find(std::string(id_or_alias)); it != id_to_index_.end()) {
        return entries_[it->second].spec.id;
    }
    if (auto alias = alias_to_id_.find(std::string(id_or_alias)); alias != alias_to_id_.end()) {
        return alias->second;
    }
    return {};
}

CommandResult CommandBus::execute(CommandInvocation invocation, CommandContext context)
{
    auto* entry = find_entry(invocation.command_id);
    if (!entry) {
        return make_result(CommandStatus::unknown_command,
            std::string{"Unknown command: "} + invocation.command_id,
            CommandOutputChannel::error);
    }

    if (has_flag(entry->spec.flags, CommandFlags::disabled)) {
        return make_result(CommandStatus::rejected,
            std::string{"Command disabled: "} + entry->spec.id,
            CommandOutputChannel::warn);
    }

    if (context.active_tab_id.empty() && context.workspace) {
        context.active_tab_id = context.workspace->active_tab_id();
    }

    invocation.command_id = entry->spec.id;
    CommandResult result = entry->handler(invocation, context);
    if (result.undo_action && context.record_undo && entry->spec.scope == CommandScope::workspace
        && is_success_for_undo(result.status) && context.workspace) {
        context.workspace->push_undo(*result.undo_action);
    }
    return result;
}

CommandResult CommandBus::execute(std::string_view id_or_alias, CommandArgs args, CommandContext context)
{
    CommandInvocation invocation;
    invocation.command_id = std::string(id_or_alias);
    invocation.args = std::move(args);
    return execute(std::move(invocation), std::move(context));
}

const CommandBus::CommandEntry* CommandBus::find_entry(std::string_view id_or_alias) const
{
    const std::string key{id_or_alias};
    if (auto it = id_to_index_.find(key); it != id_to_index_.end()) {
        return &entries_[it->second];
    }
    if (auto alias = alias_to_id_.find(key); alias != alias_to_id_.end()) {
        if (auto it = id_to_index_.find(alias->second); it != id_to_index_.end()) {
            return &entries_[it->second];
        }
    }
    return nullptr;
}

CommandBus::CommandEntry* CommandBus::find_entry(std::string_view id_or_alias)
{
    const CommandBus* const_this = this;
    return const_cast<CommandEntry*>(const_this->find_entry(id_or_alias));
}

std::string_view command_status_name(CommandStatus status) noexcept
{
    switch (status) {
    case CommandStatus::success:
        return "success";
    case CommandStatus::failed:
        return "failed";
    case CommandStatus::unknown_command:
        return "unknown_command";
    case CommandStatus::rejected:
        return "rejected";
    case CommandStatus::noop:
        return "noop";
    }
    return "unknown";
}

std::string_view command_output_channel_name(CommandOutputChannel channel) noexcept
{
    switch (channel) {
    case CommandOutputChannel::none:
        return "none";
    case CommandOutputChannel::info:
        return "info";
    case CommandOutputChannel::warn:
        return "warn";
    case CommandOutputChannel::error:
        return "error";
    case CommandOutputChannel::script:
        return "script";
    }
    return "info";
}

std::string command_arg_string(const CommandArg& arg)
{
    if (const auto* value = std::get_if<std::string>(&arg.value)) {
        return *value;
    }
    if (const auto* value = std::get_if<int64_t>(&arg.value)) {
        return std::to_string(*value);
    }
    if (const auto* value = std::get_if<bool>(&arg.value)) {
        return *value ? "true" : "false";
    }
    if (const auto* value = std::get_if<CommandResourceRef>(&arg.value)) {
        return value->type.empty() ? value->resref : value->type + ":" + value->resref;
    }
    if (const auto* value = std::get_if<CommandObjectId>(&arg.value)) {
        return value->type.empty() ? std::to_string(value->value) : value->type + ":" + std::to_string(value->value);
    }
    return {};
}

std::string command_arg_string(const CommandArgs& args, size_t index)
{
    if (index >= args.size()) {
        return {};
    }
    return command_arg_string(args[index]);
}

} // namespace nw::toolset
