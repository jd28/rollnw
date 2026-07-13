#include "Context.hpp"

#include "Smalls.hpp"

#include <fmt/format.h>
#include <rang/rang.hpp>

#include <sstream>

namespace nw::smalls {

namespace {

bool find_line_from_map(const Script* script, StringView text, size_t target_line, size_t& start, size_t& end)
{
    if (!script || script->ast_discarded() || target_line == 0) {
        return false;
    }

    const auto& line_map = script->ast().line_map;
    if (target_line > line_map.size()) {
        return false;
    }

    size_t line_start = line_map[target_line - 1];
    size_t line_end = text.size();
    if (target_line < line_map.size()) {
        line_end = line_map[target_line] == 0 ? 0 : line_map[target_line] - 1;
    }
    if (line_start > text.size() || line_end > text.size() || line_end < line_start) {
        return false;
    }
    if (line_end > line_start && text[line_end - 1] == '\r') {
        --line_end;
    }

    start = line_start;
    end = line_end;
    return true;
}

bool find_line_by_scan(StringView text, size_t target_line, size_t& start, size_t& end)
{
    if (target_line == 0) {
        return false;
    }
    size_t current_line = 1;
    size_t line_start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (current_line == target_line) {
            line_start = i;
            break;
        }
        if (text[i] == '\n') {
            current_line++;
        }
    }
    if (current_line != target_line) {
        return false;
    }

    size_t line_end = text.size();
    for (size_t i = line_start; i < text.size(); ++i) {
        if (text[i] == '\n' || text[i] == '\r') {
            line_end = i;
            break;
        }
    }
    start = line_start;
    end = line_end;
    return true;
}

bool find_line_bounds(const Script* script, StringView text, size_t target_line, size_t& start, size_t& end)
{
    return find_line_from_map(script, text, target_line, start, end)
        || find_line_by_scan(text, target_line, start, end);
}

void print_diagnostic_impl(Context* ctx, Script* script, StringView msg, bool is_warning, SourceRange range, DiagnosticType type)
{
    bool use_color = ctx ? ctx->config.use_color : true;
    if (script) {
        Diagnostic result;
        result.type = type;
        result.severity = is_warning ? DiagnosticSeverity::warning : DiagnosticSeverity::error;
        result.script = script->name();
        result.message = String(msg);
        result.location = range;
        script->add_diagnostic(std::move(result));

        if (is_warning) {
            script->increment_warnings();
        } else {
            script->increment_errors();
        }
        if (type == DiagnosticType::semantic) {
            script->increment_semantic_diagnostics();
        }
    }

    StringView path = script ? script->name() : "<source>";

    std::stringstream ss;

    auto add_style = [&](auto style) {
        if (use_color) {
            ss << style;
        }
    };

    add_style(rang::style::bold);
    ss << path << ":" << range.start.line << ":" << range.start.column << ": ";

    if (is_warning) {
        add_style(rang::fg::yellow);
        ss << "warning";
    } else {
        add_style(rang::fg::red);
        ss << "error";
    }

    add_style(rang::fg::reset);
    ss << ": " << msg;
    add_style(rang::style::reset);

    if (script && range.start.line > 0) {
        StringView text = script->text();
        bool show_multiline = type != DiagnosticType::lexical;

        size_t line_start = 0;
        size_t line_end = 0;
        if (find_line_bounds(script, text, range.start.line, line_start, line_end)) {
            if (show_multiline && range.start.line > 1) {
                size_t prev_start = 0;
                size_t prev_end = 0;
                if (find_line_bounds(script, text, range.start.line - 1, prev_start, prev_end)) {
                    StringView prev_line = text.substr(prev_start, prev_end - prev_start);
                    ss << "\n    " << prev_line;
                }
            }

            StringView line_view = text.substr(line_start, line_end - line_start);
            ss << "\n    " << line_view << "\n    ";

            for (size_t i = 0; i < range.start.column; ++i) {
                if (i < line_view.size() && line_view[i] == '\t') {
                    ss << '\t';
                } else {
                    ss << ' ';
                }
            }

            if (is_warning) {
                add_style(rang::fg::yellow);
            } else {
                add_style(rang::fg::red);
            }

            ss << "^";

            size_t len = 0;
            if (range.end.line == range.start.line && range.end.column > range.start.column) {
                len = range.end.column - range.start.column - 1;
            }

            if (len > 80) {
                len = 80;
            }
            for (size_t i = 0; i < len; ++i) {
                ss << "~";
            }
            add_style(rang::fg::reset);

            if (show_multiline && range.end.line > range.start.line) {
                ss << "\n    (range spans " << (range.end.line - range.start.line + 1) << " lines)";
            }

            if (show_multiline) {
                size_t next_start = 0;
                size_t next_end = 0;
                if (find_line_bounds(script, text, range.start.line + 1, next_start, next_end)) {
                    StringView next_line = text.substr(next_start, next_end - next_start);
                    ss << "\n    " << next_line;
                }
            }
        }
    }

    if (is_warning) {
        LOG_F(WARNING, "{}", ss.str());
    } else {
        LOG_F(ERROR, "{}", ss.str());
    }
}

} // namespace

void Context::lexical_diagnostic(Script* script, StringView msg, bool is_warning, SourceRange range)
{
    print_diagnostic_impl(this, script, msg, is_warning, range, DiagnosticType::lexical);
}

void Context::parse_diagnostic(Script* script, StringView msg, bool is_warning, SourceRange range)
{
    print_diagnostic_impl(this, script, msg, is_warning, range, DiagnosticType::parse);
}

bool Context::will_emit_semantic_diagnostic(const Script* script) const noexcept
{
    return !script || limits.max_semantic_diagnostics == 0
        || script->semantic_diagnostics() < limits.max_semantic_diagnostics;
}

void Context::semantic_diagnostic(Script* script, StringView msg, bool is_warning, SourceRange range)
{
    if (script && limits.max_semantic_diagnostics != 0
        && script->semantic_diagnostics() >= limits.max_semantic_diagnostics) {
        if (!script->semantic_diagnostic_limit_reported()) {
            script->mark_semantic_diagnostic_limit_reported();
            String limit_message = fmt::format(
                "too many semantic diagnostics, suppressing further diagnostics (limit {})",
                limits.max_semantic_diagnostics);
            print_diagnostic_impl(this, script, limit_message, false, range, DiagnosticType::semantic);
        }
        return;
    }
    print_diagnostic_impl(this, script, msg, is_warning, range, DiagnosticType::semantic);
}

} // nw::smalls
