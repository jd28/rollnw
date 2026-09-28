#include "project_ignore.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string_view>

namespace nw::toolset {
namespace {

void append_literal(std::string& expression, char ch)
{
    if (std::string_view{R"(\.^$|()[]{}+*?)"}.find(ch) != std::string_view::npos) {
        expression += '\\';
    }
    expression += ch;
}

std::optional<std::regex> compile_pattern(std::string_view pattern)
{
    std::string expression;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (ch == '\\') {
            if (++i == pattern.size()) { return std::nullopt; }
            append_literal(expression, pattern[i]);
        } else if (ch == '?') {
            expression += "[^/]";
        } else if (ch == '*') {
            const size_t start = i;
            while (i + 1 < pattern.size() && pattern[i + 1] == '*') {
                ++i;
            }
            const bool directory_star = i > start && (start == 0 || pattern[start - 1] == '/');
            if (directory_star && i + 1 < pattern.size() && pattern[i + 1] == '/') {
                expression += "(?:[^/]+/)*";
                ++i;
            } else if (directory_star && i + 1 == pattern.size()) {
                expression += "[\\s\\S]*";
            } else {
                expression += "[^/]*";
            }
        } else if (ch == '[') {
            expression += "(?!/)[";
            ++i;
            if (i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^')) {
                expression += '^';
                ++i;
            }
            if (i < pattern.size() && pattern[i] == ']') {
                expression += R"(\])";
                ++i;
            }
            for (; i < pattern.size() && pattern[i] != ']'; ++i) {
                if (pattern[i] == '[' && i + 1 < pattern.size() && pattern[i + 1] == ':') {
                    const auto end = pattern.find(":]", i + 2);
                    if (end == pattern.npos) { return std::nullopt; }
                    expression += pattern.substr(i, end + 2 - i);
                    i = end + 1;
                } else if (pattern[i] == '\\') {
                    if (++i == pattern.size()) { return std::nullopt; }
                    if (pattern[i] == '-') { expression += '\\'; }
                    append_literal(expression, pattern[i]);
                } else {
                    expression += pattern[i];
                }
            }
            if (i == pattern.size()) { return std::nullopt; }
            expression += ']';
        } else {
            append_literal(expression, ch);
        }
    }
    try {
        return std::regex{expression, std::regex::ECMAScript | std::regex::nosubs};
    } catch (const std::regex_error&) {
        return std::nullopt;
    }
}

} // namespace

bool append_project_ignore_rules(const std::filesystem::path& directory,
    const std::filesystem::path& relative_directory,
    std::vector<ProjectIgnoreRule>& rules, std::string& error)
{
    const auto file = directory / ".gitignore";
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(file, ec);
    if (ec == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found) { return true; }
    if (ec) {
        error = "Failed to read " + file.string() + ": " + ec.message();
        return false;
    }
    if (!std::filesystem::is_regular_file(status)) { return true; }
    std::ifstream input{file, std::ios::binary};
    if (!input) {
        error = "Failed to read " + file.string();
        return false;
    }
    const auto base = relative_directory.generic_string();
    std::string line;
    bool first_line = true;
    while (std::getline(input, line)) {
        if (first_line && line.starts_with("\xef\xbb\xbf")) { line.erase(0, 3); }
        first_line = false;
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        while (!line.empty() && line.back() == ' ') {
            size_t escapes = 0;
            for (size_t i = line.size() - 1; i > 0 && line[i - 1] == '\\'; --i) {
                ++escapes;
            }
            if (escapes % 2 != 0) { break; }
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') { continue; }
        std::string_view pattern{line};
        const bool include = pattern.front() == '!';
        if (include) { pattern.remove_prefix(1); }
        if (pattern.empty()) { continue; }
        const bool directories_only = pattern.back() == '/';
        if (directories_only) { pattern.remove_suffix(1); }
        const bool basename_only = pattern.find('/') == pattern.npos;
        if (pattern.starts_with('/')) { pattern.remove_prefix(1); }
        if (pattern.empty()) { continue; }
        auto compiled = compile_pattern(pattern);
        if (compiled) {
            rules.push_back({std::move(*compiled), base.empty() ? 0 : base.size() + 1,
                basename_only, directories_only, include});
        }
    }
    if (input.bad()) {
        error = "Failed to read " + file.string();
        return false;
    }
    return true;
}

void filter_project_entries(const std::filesystem::path& relative_directory,
    std::span<const ProjectIgnoreRule> rules,
    std::vector<std::filesystem::directory_entry>& entries)
{
    if (rules.empty()) { return; }
    std::erase_if(entries, [&](const auto& entry) {
        const auto filename = entry.path().filename().generic_string();
        const auto relative = (relative_directory / entry.path().filename()).generic_string();
        std::error_code ec;
        const bool directory = !entry.is_symlink(ec) && entry.is_directory(ec);
        for (auto it = rules.rbegin(); it != rules.rend(); ++it) {
            if (it->directories_only && !directory) { continue; }
            const auto candidate = it->basename_only ? std::string_view{filename}
                                                     : std::string_view{relative}.substr(it->base_length);
            if (std::regex_match(candidate.begin(), candidate.end(), it->pattern)) {
                return !it->include;
            }
        }
        return false;
    });
}

} // namespace nw::toolset
