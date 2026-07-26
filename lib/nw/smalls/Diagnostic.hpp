#pragma once

#include "SourceLocation.hpp"

#include <cstdint>

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

/// A stable identifier for the rule that produced a diagnostic.
///
/// Tooling keys on this rather than on message text, so rewording a message
/// cannot silently disable a quick fix or a suppression.
///
/// The space is per-category with named rules layered on top, because that is
/// what the emission sites support: 47 of the 81 sites in this library are
/// parse errors that all mean "syntax error", and naming each expected-token
/// variant separately would produce codes nothing consumes. A site that has not
/// been classified still reports its category, so every diagnostic carries a
/// code.
enum struct DiagnosticCode : uint16_t {
    none = 0,

    // Categories. The default for an unclassified site.
    lexical,
    syntax,
    semantic,

    // Named rules. Add one when a consumer needs to recognize the rule.
    unresolved_identifier,
    unknown_type,
    unknown_field,
    module_load_failed,
    duplicate_declaration,
    argument_count_mismatch,
    argument_type_mismatch,
    const_assignment,
};

/// The wire form of a code, as published in an LSP diagnostic.
StringView diagnostic_code_name(DiagnosticCode code) noexcept;

/// The category a type falls back to when a site names no rule.
DiagnosticCode diagnostic_category(DiagnosticType type) noexcept;

/// A second location a diagnostic refers to, such as a prior declaration.
struct DiagnosticRelated {
    String script;
    String message;
    SourceRange location;
};

struct Diagnostic {
    DiagnosticType type;
    DiagnosticSeverity severity;
    DiagnosticCode code = DiagnosticCode::none;
    String script;
    String message;
    SourceRange location;
    Vector<DiagnosticRelated> related;
    /// Names a fix may offer in place of the one that failed to resolve.
    Vector<String> candidates;
};

/// Extra data an emission site can attach beyond a message and a range.
struct DiagnosticInfo {
    DiagnosticCode code = DiagnosticCode::none;
    Vector<DiagnosticRelated> related;
    Vector<String> candidates;
};

size_t edit_distance(StringView a, StringView b);

/// Ranked near-matches for a misspelled name, best first, at most three.
/// A quick fix needs the names themselves, not the prose form below.
Vector<String> suggestion_candidates(StringView needle, const Vector<StringView>& candidates);

String format_suggestions(StringView needle, const Vector<StringView>& candidates);

} // namespace nw::smalls
