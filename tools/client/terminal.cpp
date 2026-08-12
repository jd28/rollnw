#include "terminal.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace nw::toolset {

namespace {

void trim_ascii(std::string_view& value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
}

std::string to_lower_ascii(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool starts_with_casefold(std::string_view value, std::string_view prefix)
{
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(value[i]);
        const auto rhs = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

bool command_matches_prefix(const CommandSpec& spec, std::string_view prefix)
{
    if (starts_with_casefold(spec.id, prefix)) {
        return true;
    }
    for (const auto& alias : spec.aliases) {
        if (starts_with_casefold(alias, prefix)) {
            return true;
        }
    }
    return false;
}

std::string common_prefix(std::string lhs, std::string_view rhs)
{
    const size_t limit = std::min(lhs.size(), rhs.size());
    size_t i = 0;
    while (i < limit
        && std::tolower(static_cast<unsigned char>(lhs[i])) == std::tolower(static_cast<unsigned char>(rhs[i]))) {
        lhs[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lhs[i])));
        ++i;
    }
    lhs.resize(i);
    return lhs;
}

std::pair<size_t, size_t> command_token_span(std::string_view line)
{
    size_t start = 0;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
        ++start;
    }
    if (start < line.size() && line[start] == ':') {
        ++start;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
            ++start;
        }
    }

    size_t end = start;
    while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
        ++end;
    }
    return {start, end};
}

CommandResult parse_error(std::string message)
{
    CommandResult result;
    result.status = CommandStatus::failed;
    result.message = std::move(message);
    result.output_channel = CommandOutputChannel::error;
    return result;
}

} // namespace

TerminalParseResult TerminalDispatcher::parse(std::string_view line) const
{
    trim_ascii(line);
    if (!line.empty() && line.front() == ':') {
        line.remove_prefix(1);
        trim_ascii(line);
    }
    if (line.empty()) {
        TerminalParseResult result;
        result.empty = true;
        return result;
    }

    std::vector<std::string> tokens;
    std::string current;
    char quote = '\0';
    bool escaping = false;

    for (const char ch : line) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }

        if (ch == '\\' && quote != '\0') {
            escaping = true;
            continue;
        }

        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            } else {
                current.push_back(ch);
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (escaping) {
        current.push_back('\\');
    }
    if (quote != '\0') {
        TerminalParseResult result;
        result.error = "Unterminated quoted argument";
        return result;
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }

    if (tokens.empty()) {
        TerminalParseResult result;
        result.empty = true;
        return result;
    }

    TerminalParseResult result;
    result.invocation.command_id = std::move(tokens.front());
    result.invocation.args.reserve(tokens.size() - 1);
    for (size_t i = 1; i < tokens.size(); ++i) {
        result.invocation.args.push_back(CommandArg::positional_string(std::move(tokens[i])));
    }
    return result;
}

TerminalCompletionResult TerminalDispatcher::complete(const CommandBus& bus, std::string_view line) const
{
    return complete(bus, line, line.size());
}

TerminalCompletionResult TerminalDispatcher::complete(const CommandBus& bus, std::string_view line, size_t cursor_byte_position) const
{
    const auto [token_start, token_end] = command_token_span(line);
    cursor_byte_position = std::min(cursor_byte_position, line.size());
    if (cursor_byte_position < token_start || cursor_byte_position > token_end) {
        return {};
    }

    const std::string_view prefix = line.substr(token_start, cursor_byte_position - token_start);

    TerminalCompletionResult result;
    auto commands = bus.list_commands();
    commands.erase(std::remove_if(commands.begin(), commands.end(), [prefix](const CommandSpec& spec) {
        return !command_matches_prefix(spec, prefix);
    }),
        commands.end());

    if (commands.empty()) {
        return result;
    }

    std::sort(commands.begin(), commands.end(), [](const CommandSpec& lhs, const CommandSpec& rhs) {
        return to_lower_ascii(lhs.id) < to_lower_ascii(rhs.id);
    });
    result.candidates = std::move(commands);

    std::string completion = result.candidates.front().id;
    for (size_t i = 1; i < result.candidates.size(); ++i) {
        completion = common_prefix(std::move(completion), result.candidates[i].id);
    }

    if (result.candidates.size() == 1) {
        completion = result.candidates.front().id;
    } else {
        result.ambiguous = true;
    }

    if (completion.empty() || completion.size() <= prefix.size()) {
        return result;
    }

    std::string replacement{line};
    replacement.replace(token_start, token_end - token_start, completion);
    result.cursor_byte_position = token_start + completion.size();
    if (result.candidates.size() == 1 && token_end == line.size()) {
        replacement.push_back(' ');
        ++result.cursor_byte_position;
    }

    result.completed = true;
    result.replacement = std::move(replacement);
    return result;
}

CommandResult TerminalDispatcher::execute(CommandBus& bus, std::string_view line, CommandContext context) const
{
    TerminalParseResult parsed = parse(line);
    if (!parsed.error.empty()) {
        return parse_error(std::move(parsed.error));
    }
    if (parsed.empty) {
        CommandResult result;
        result.status = CommandStatus::noop;
        result.output_channel = CommandOutputChannel::none;
        return result;
    }

    context.source = CommandSource::terminal;
    return bus.execute(std::move(parsed.invocation), std::move(context));
}

} // namespace nw::toolset
