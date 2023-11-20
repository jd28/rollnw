#include "LspContext.hpp"

#include "Nss.hpp"

namespace nw::script {

LspContext::LspContext(std::string command_script)
    : Context(std::move(command_script))
{
}

const std::vector<Diagnostic>& LspContext::diagnostics() const
{
    return diagnostics_;
}

void LspContext::lexical_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc)
{
    Diagnostic result;
    result.type = DiagnosticType::lexical;
    result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
    result.script = script ? script->name() : "<source>";
    result.message = std::string(msg);
    result.location = loc;
    diagnostics_.push_back(std::move(result));
}

void LspContext::parse_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc)
{
    Diagnostic result;
    result.type = DiagnosticType::parse;
    result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
    result.script = script ? script->name() : "<source>";
    result.message = std::string(msg);
    result.location = loc;
    diagnostics_.push_back(std::move(result));
}

void LspContext::semantic_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc)
{
    Diagnostic result;
    result.type = DiagnosticType::semantic;
    result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
    result.script = script ? script->name() : "<source>";
    result.message = std::string(msg);
    result.location = loc;
    diagnostics_.push_back(std::move(result));
}

} // namespace nw::script
