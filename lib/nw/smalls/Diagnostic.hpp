#pragma once

#include "SourceLocation.hpp"

namespace nw::smalls {

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

size_t edit_distance(StringView a, StringView b);
String format_suggestions(StringView needle, const Vector<StringView>& candidates);

} // namespace nw::smalls
