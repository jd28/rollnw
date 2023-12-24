#include "LspContext.hpp"

#include "Nss.hpp"

#include "../log.hpp"

namespace nw::script {

LspContext::LspContext(std::vector<std::string> include_paths, std::string command_script)
    : Context(std::move(include_paths), std::move(command_script))
{
}

void LspContext::lexical_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range)
{
    CHECK_F(!!script, "");

    Diagnostic result;
    result.type = DiagnosticType::lexical;
    result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
    result.script = script ? script->name() : "<source>";
    result.message = std::string(msg);
    result.location = range;
    script->add_diagnostic(std::move(result));

    if (is_warning) {
        script->increment_warnings();
    } else {
        script->increment_errors();
    }
}

void LspContext::parse_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range)
{
    CHECK_F(!!script, "");

    Diagnostic result;
    result.type = DiagnosticType::parse;
    result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
    result.script = script ? script->name() : "<source>";
    result.message = std::string(msg);
    result.location = range;
    script->add_diagnostic(std::move(result));

    if (is_warning) {
        script->increment_warnings();
    } else {
        script->increment_errors();
    }
}

void LspContext::semantic_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range)
{
    CHECK_F(!!script, "");

    Diagnostic result;
    result.type = DiagnosticType::semantic;
    result.level = is_warning ? DiagnosticLevel::warning : DiagnosticLevel::error;
    result.script = script ? script->name() : "<source>";
    result.message = std::string(msg);
    result.location = range;
    script->add_diagnostic(std::move(result));

    if (is_warning) {
        script->increment_warnings();
    } else {
        script->increment_errors();
    }
}

} // namespace nw::script
