#pragma once

#include "SourceLocation.hpp"

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
    SourceRange location;
};

} // namespace nw::script
