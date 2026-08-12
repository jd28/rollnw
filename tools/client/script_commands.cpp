#include "script_commands.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <sstream>
#include <utility>

namespace nw::toolset {

namespace {

using nw::smalls::FunctionMetadata;
using nw::smalls::ModuleInterface;
using nw::smalls::NativeFunction;
using nw::smalls::ParamMetadata;
using nw::smalls::Runtime;
using nw::smalls::Value;
using nw::smalls::ValueStorage;

bool value_to_string(Runtime& rt, const Value& value, std::string& out)
{
    if (value.type_id != rt.string_type() || value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    out = std::string(rt.get_string_view(value.data.hptr));
    return true;
}

bool value_to_int(Runtime& rt, const Value& value, int& out)
{
    if (value.type_id != rt.int_type()) {
        return false;
    }
    out = value.data.ival;
    return true;
}

bool read_struct_string_field(Runtime& rt, const Value& value, std::string_view field, std::string& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const Value field_value = rt.read_struct_field(value.data.hptr, value.type_id, field);
    return value_to_string(rt, field_value, out);
}

bool read_struct_int_field(Runtime& rt, const Value& value, std::string_view field, int& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const Value field_value = rt.read_struct_field(value.data.hptr, value.type_id, field);
    return value_to_int(rt, field_value, out);
}

bool decode_string_array(Runtime& rt, const Value& value, std::vector<std::string>& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }

    out.clear();
    const size_t count = rt.array_size(value.data.hptr);
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Value item;
        if (!rt.array_get(value.data.hptr, static_cast<uint32_t>(i), item)) {
            return false;
        }
        std::string text;
        if (!value_to_string(rt, item, text)) {
            return false;
        }
        out.push_back(std::move(text));
    }
    return true;
}

bool read_struct_string_array_field(Runtime& rt, const Value& value, std::string_view field, std::vector<std::string>& out)
{
    if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
        return false;
    }
    const Value field_value = rt.read_struct_field(value.data.hptr, value.type_id, field);
    return decode_string_array(rt, field_value, out);
}

CommandScope command_scope_from_int(int value) noexcept
{
    switch (value) {
    case 1:
        return CommandScope::workspace;
    case 2:
        return CommandScope::renderer;
    default:
        return CommandScope::global;
    }
}

CommandStatus command_status_from_int(int value) noexcept
{
    switch (value) {
    case 1:
        return CommandStatus::failed;
    case 2:
        return CommandStatus::unknown_command;
    case 3:
        return CommandStatus::rejected;
    case 4:
        return CommandStatus::noop;
    default:
        return CommandStatus::success;
    }
}

CommandOutputChannel command_channel_from_string(std::string_view value) noexcept
{
    if (value == "none") {
        return CommandOutputChannel::none;
    }
    if (value == "warn") {
        return CommandOutputChannel::warn;
    }
    if (value == "error") {
        return CommandOutputChannel::error;
    }
    if (value == "script") {
        return CommandOutputChannel::script;
    }
    return CommandOutputChannel::info;
}

CommandResult make_script_result(CommandStatus status, std::string message, CommandOutputChannel channel)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = channel;
    return result;
}

bool decode_command_spec(Runtime& rt, const Value& value, CommandSpec& out)
{
    int scope = 0;
    int flags = 0;
    return read_struct_string_field(rt, value, "id", out.id)
        && read_struct_string_field(rt, value, "title", out.title)
        && read_struct_string_field(rt, value, "description", out.description)
        && read_struct_string_field(rt, value, "category", out.category)
        && read_struct_string_array_field(rt, value, "aliases", out.aliases)
        && read_struct_int_field(rt, value, "scope", scope)
        && read_struct_int_field(rt, value, "flags", flags)
        && read_struct_string_field(rt, value, "default_binding", out.default_binding)
        && read_struct_string_field(rt, value, "usage", out.usage)
        && (out.scope = command_scope_from_int(scope), out.flags = static_cast<CommandFlags>(flags), true);
}

std::string join_args_for_script(const CommandArgs& args)
{
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out << '\n';
        }
        out << command_arg_string(args[i]);
    }
    return out.str();
}

Value make_string(Runtime& rt, std::string_view value)
{
    return Value::make_string(rt.alloc_string(value));
}

Value encode_command_result(Runtime& rt, const CommandResult& result, std::string_view undo_token = {})
{
    const auto result_type = rt.type_id("core.commands.CommandResult", false);
    if (result_type == nw::smalls::invalid_type_id) {
        return make_string(rt, result.message);
    }

    const auto ptr = rt.alloc_struct(result_type);
    if (ptr.value == 0) {
        return {};
    }

    if (!rt.write_struct_field(ptr, result_type, "status", Value::make_int(static_cast<int32_t>(result.status)))
        || !rt.write_struct_field(ptr, result_type, "message", make_string(rt, result.message))
        || !rt.write_struct_field(ptr, result_type, "channel", make_string(rt, command_output_channel_name(result.output_channel)))
        || !rt.write_struct_field(ptr, result_type, "undo_token", make_string(rt, undo_token))) {
        return {};
    }
    return Value::make_heap(ptr, result_type);
}

CommandResult decode_command_result(Runtime& rt, const Value& value, std::string& undo_token)
{
    if (value.type_id == rt.string_type()) {
        std::string message;
        if (value_to_string(rt, value, message)) {
            return make_script_result(CommandStatus::success, std::move(message), CommandOutputChannel::script);
        }
    }

    int status = 0;
    std::string message;
    std::string channel;
    std::string token;
    if (read_struct_int_field(rt, value, "status", status)
        && read_struct_string_field(rt, value, "message", message)
        && read_struct_string_field(rt, value, "channel", channel)
        && read_struct_string_field(rt, value, "undo_token", token)) {
        undo_token = std::move(token);
        return make_script_result(command_status_from_int(status), std::move(message), command_channel_from_string(channel));
    }

    return make_script_result(CommandStatus::failed, "Script command returned an invalid result", CommandOutputChannel::error);
}

FunctionMetadata make_meta(std::string_view name, nw::smalls::TypeID ret,
    std::initializer_list<std::pair<const char*, nw::smalls::TypeID>> params)
{
    FunctionMetadata meta;
    meta.name = std::string(name);
    meta.return_type = ret;
    for (const auto& [param_name, param_type] : params) {
        meta.params.push_back(ParamMetadata{std::string(param_name), param_type});
    }
    return meta;
}

} // namespace

void ScriptCommandHost::bind(CommandBus* bus, WorkspaceState* workspace) noexcept
{
    bus_ = bus;
    workspace_ = workspace;
}

bool ScriptCommandHost::register_command(std::string module_path,
    CommandSpec spec,
    std::string execute_handler,
    std::string undo_handler,
    std::string redo_handler)
{
    if (!bus_ || module_path.empty() || execute_handler.empty()) {
        return false;
    }

    spec.flags = spec.flags | CommandFlags::script;
    auto binding = std::make_shared<ScriptCommandBinding>(ScriptCommandBinding{
        spec,
        std::move(module_path),
        std::move(execute_handler),
        std::move(undo_handler),
        std::move(redo_handler),
    });

    CommandSpec bus_spec = binding->spec;
    std::string error;
    const bool registered = bus_->register_command(std::move(bus_spec), [binding](const CommandInvocation& invocation, CommandContext context) {
            std::vector<std::string> args{
                invocation.command_id,
                join_args_for_script(invocation.args),
            };
            return script_command_host().call_handler(*binding, binding->execute_handler, args, context); }, &error);

    if (!registered) {
        return false;
    }
    bindings_.push_back(std::move(binding));
    return true;
}

CommandResult ScriptCommandHost::execute_command(std::string_view command_id, CommandArgs args, CommandContext context)
{
    if (!bus_) {
        return make_script_result(CommandStatus::failed, "Command bus unavailable", CommandOutputChannel::error);
    }
    context.source = CommandSource::script;
    context.workspace = workspace_;
    return bus_->execute(command_id, std::move(args), context);
}

CommandResult ScriptCommandHost::call_handler(const ScriptCommandBinding& binding,
    std::string_view handler,
    const std::vector<std::string>& args,
    CommandContext context)
{
    (void)context;
    auto& rt = nw::kernel::runtime();
    nw::Vector<Value> script_args;
    script_args.reserve(args.size());
    for (const auto& arg : args) {
        script_args.push_back(make_string(rt, arg));
    }

    const auto result = rt.execute_script(binding.module_path, handler, script_args);
    if (!result.ok()) {
        return make_script_result(CommandStatus::failed,
            std::string{"Smalls error: "} + result.error_message,
            CommandOutputChannel::error);
    }

    std::string undo_token;
    CommandResult decoded = decode_command_result(rt, result.value, undo_token);
    if (decoded.ok() && !undo_token.empty() && !binding.undo_handler.empty() && !binding.redo_handler.empty()) {
        auto action = std::make_shared<CommandUndoAction>();
        action->label = binding.spec.title.empty() ? binding.spec.id : binding.spec.title;
        action->undo = [module = binding.module_path, handler_name = binding.undo_handler, undo_token](CommandContext undo_context) {
            ScriptCommandBinding undo_binding;
            undo_binding.module_path = module;
            return script_command_host().call_handler(undo_binding, handler_name, {undo_token}, undo_context);
        };
        action->redo = [module = binding.module_path, handler_name = binding.redo_handler, undo_token](CommandContext redo_context) {
            ScriptCommandBinding redo_binding;
            redo_binding.module_path = module;
            return script_command_host().call_handler(redo_binding, handler_name, {undo_token}, redo_context);
        };
        decoded.undo_action = std::move(action);
    }
    return decoded;
}

ScriptCommandHost& script_command_host()
{
    static ScriptCommandHost host;
    return host;
}

void register_smalls_commands_v1(Runtime& rt)
{
    if (rt.get_native_module("core.commands.v1")) {
        return;
    }

    const auto spec_type = rt.type_id("core.commands.CommandSpec", false);
    const auto result_type = rt.type_id("core.commands.CommandResult", false);
    const auto spec_param = spec_type == nw::smalls::invalid_type_id ? rt.any_type() : spec_type;
    const auto result_ret = result_type == nw::smalls::invalid_type_id ? rt.any_type() : result_type;

    auto register_meta = make_meta("command_register", rt.bool_type(), {
                                                                           {"module_path", rt.string_type()},
                                                                           {"spec", spec_param},
                                                                           {"execute_handler", rt.string_type()},
                                                                           {"undo_handler", rt.string_type()},
                                                                           {"redo_handler", rt.string_type()},
                                                                       });
    auto execute_meta = make_meta("command_execute", result_ret, {
                                                                     {"command_id", rt.string_type()},
                                                                     {"args", rt.any_array_type()},
                                                                 });

    ModuleInterface iface;
    iface.module_path = "core.commands.v1";
    iface.functions = {
        register_meta,
        execute_meta,
    };
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.commands.v1.command_register",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 5) {
                return Value::make_bool(false);
            }

            std::string module_path;
            CommandSpec spec;
            std::string execute_handler;
            std::string undo_handler;
            std::string redo_handler;
            if (!value_to_string(*runtime, args[0], module_path)
                || !decode_command_spec(*runtime, args[1], spec)
                || !value_to_string(*runtime, args[2], execute_handler)
                || !value_to_string(*runtime, args[3], undo_handler)
                || !value_to_string(*runtime, args[4], redo_handler)) {
                return Value::make_bool(false);
            }

            return Value::make_bool(script_command_host().register_command(std::move(module_path),
                std::move(spec),
                std::move(execute_handler),
                std::move(undo_handler),
                std::move(redo_handler)));
        },
        .metadata = std::move(register_meta),
    });

    rt.register_native_function(NativeFunction{
        .name = "core.commands.v1.command_execute",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) {
                return {};
            }

            std::string command_id;
            std::vector<std::string> raw_args;
            if (!value_to_string(*runtime, args[0], command_id) || !decode_string_array(*runtime, args[1], raw_args)) {
                return encode_command_result(*runtime,
                    make_script_result(CommandStatus::failed, "Invalid command_execute arguments", CommandOutputChannel::error));
            }

            CommandArgs command_args;
            command_args.reserve(raw_args.size());
            for (auto& arg : raw_args) {
                command_args.push_back(CommandArg::positional_string(std::move(arg)));
            }

            CommandContext context;
            context.source = CommandSource::script;
            CommandResult result = script_command_host().execute_command(command_id, std::move(command_args), context);
            return encode_command_result(*runtime, result);
        },
        .metadata = std::move(execute_meta),
    });
}

} // namespace nw::toolset
