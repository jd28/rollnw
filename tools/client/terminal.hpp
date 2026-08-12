#pragma once

#include "command_bus.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

struct TerminalParseResult {
    bool empty = false;
    CommandInvocation invocation;
    std::string error;
};

struct TerminalCompletionResult {
    bool completed = false;
    bool ambiguous = false;
    std::string replacement;
    size_t cursor_byte_position = 0;
    std::vector<CommandSpec> candidates;
};

class TerminalDispatcher {
public:
    [[nodiscard]] TerminalParseResult parse(std::string_view line) const;
    [[nodiscard]] TerminalCompletionResult complete(const CommandBus& bus, std::string_view line) const;
    [[nodiscard]] TerminalCompletionResult complete(const CommandBus& bus, std::string_view line, size_t cursor_byte_position) const;
    CommandResult execute(CommandBus& bus, std::string_view line, CommandContext context) const;
};

} // namespace nw::toolset
