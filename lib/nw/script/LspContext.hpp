#pragma once

#include "Context.hpp"

namespace nw::script {

enum struct DiagnosticType {
    lexical,
    parse,
    semantic,
};

enum struct DiagnosticLevel {
    warning,
    error,
};

struct Diagnostic {
    DiagnosticType type;
    DiagnosticLevel level;
    std::string script;
    std::string message;
    SourceLocation location;
};

struct LspContext : public Context {
    LspContext(std::string command_script = "nwscript");
    virtual ~LspContext() = default;

    std::vector<Diagnostic> diagnostics_;

    const std::vector<Diagnostic>& diagnostics() const;

    virtual void lexical_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc) override;
    virtual void parse_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc) override;
    virtual void semantic_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc) override;
};

} // namespace nw::script
