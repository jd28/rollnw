#include "smalls_diagnostics.hpp"

#include <nw/smalls/Diagnostic.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/format.h>

namespace nw::toolset {

namespace {

std::string_view severity_label(nw::smalls::DiagnosticSeverity severity)
{
    switch (severity) {
    case nw::smalls::DiagnosticSeverity::error:
        return "error";
    case nw::smalls::DiagnosticSeverity::warning:
        return "warning";
    case nw::smalls::DiagnosticSeverity::information:
        return "information";
    case nw::smalls::DiagnosticSeverity::hint:
        return "hint";
    }
    return "diagnostic";
}

size_t mapped_line(size_t line, size_t first_source_line)
{
    return line > 0 ? first_source_line + line - 1 : first_source_line;
}

} // namespace

std::string format_smalls_diagnostic(const nw::smalls::Diagnostic& diagnostic,
    std::string_view source_path, size_t first_source_line)
{
    if (source_path.empty()) {
        source_path = diagnostic.script;
    }
    return fmt::format("{}:{}:{}: {}: {}",
        source_path,
        mapped_line(diagnostic.location.start.line, first_source_line),
        diagnostic.location.start.column,
        severity_label(diagnostic.severity),
        diagnostic.message);
}

std::string format_smalls_execution_error(const nw::smalls::ExecutionResult& result,
    std::string_view source_path, size_t first_source_line)
{
    if (source_path.empty()) {
        source_path = result.error_module.empty() ? std::string_view{"<smalls>"} : std::string_view{result.error_module};
    }

    std::string message = fmt::format("{}:{}:{}: error: {}",
        source_path,
        mapped_line(result.error_location.range.start.line, first_source_line),
        result.error_location.range.start.column,
        result.error_message);
    if (!result.error_snippet.empty()) {
        message += fmt::format("\n  {}", result.error_snippet);
    }
    if (!result.stack_trace.empty()) {
        message += fmt::format("\n{}", result.stack_trace);
    }
    return message;
}

} // namespace nw::toolset
