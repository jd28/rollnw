#pragma once

#include "SourceLocation.hpp"

#include <string>

namespace nw::script {

enum struct DiagnosticType {
    lexical,
    parse,
    semantic,
};

enum struct DiagnosticSeverity {
    error,
    hint,
    information,
    warning,
};

struct Diagnostic {
    DiagnosticType type;
    DiagnosticSeverity severity;
    String script;
    String message;
    SourceRange location;
};

} // namespace nw::script
