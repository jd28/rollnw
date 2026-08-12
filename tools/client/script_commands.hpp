#pragma once

#include "command_bus.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

class WorkspaceState;

struct ScriptCommandBinding {
    CommandSpec spec;
    std::string module_path;
    std::string execute_handler;
    std::string undo_handler;
    std::string redo_handler;
};

class ScriptCommandHost {
public:
    void bind(CommandBus* bus, WorkspaceState* workspace) noexcept;

    bool register_command(std::string module_path,
        CommandSpec spec,
        std::string execute_handler,
        std::string undo_handler,
        std::string redo_handler);

    CommandResult execute_command(std::string_view command_id, CommandArgs args, CommandContext context);
    CommandResult call_handler(const ScriptCommandBinding& binding,
        std::string_view handler,
        const std::vector<std::string>& args,
        CommandContext context);

private:
    CommandBus* bus_ = nullptr;
    WorkspaceState* workspace_ = nullptr;
    std::vector<std::shared_ptr<ScriptCommandBinding>> bindings_;
};

ScriptCommandHost& script_command_host();
void register_smalls_commands_v1(nw::smalls::Runtime& rt);

} // namespace nw::toolset
