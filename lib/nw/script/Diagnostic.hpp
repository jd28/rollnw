#pragma once

#include "Token.hpp"

#include <string>

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

} // namespace nw::script
