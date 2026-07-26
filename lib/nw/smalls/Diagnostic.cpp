#include "Diagnostic.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace nw::smalls {

StringView diagnostic_code_name(DiagnosticCode code) noexcept
{
    switch (code) {
    case DiagnosticCode::lexical:
        return "lexical";
    case DiagnosticCode::syntax:
        return "syntax";
    case DiagnosticCode::semantic:
        return "semantic";
    case DiagnosticCode::unresolved_identifier:
        return "unresolved-identifier";
    case DiagnosticCode::unknown_type:
        return "unknown-type";
    case DiagnosticCode::unknown_field:
        return "unknown-field";
    case DiagnosticCode::module_load_failed:
        return "module-load-failed";
    case DiagnosticCode::duplicate_declaration:
        return "duplicate-declaration";
    case DiagnosticCode::argument_count_mismatch:
        return "argument-count-mismatch";
    case DiagnosticCode::argument_type_mismatch:
        return "argument-type-mismatch";
    case DiagnosticCode::const_assignment:
        return "const-assignment";
    case DiagnosticCode::none:
        break;
    }
    return {};
}

DiagnosticCode diagnostic_category(DiagnosticType type) noexcept
{
    switch (type) {
    case DiagnosticType::lexical:
        return DiagnosticCode::lexical;
    case DiagnosticType::parse:
        return DiagnosticCode::syntax;
    case DiagnosticType::semantic:
        return DiagnosticCode::semantic;
    }
    return DiagnosticCode::none;
}

size_t edit_distance(StringView a, StringView b)
{
    size_t n = a.size();
    size_t m = b.size();
    if (n == 0) { return m; }
    if (m == 0) { return n; }

    Vector<size_t> prev(m + 1);
    Vector<size_t> curr(m + 1);
    for (size_t j = 0; j <= m; ++j) {
        prev[j] = j;
    }

    for (size_t i = 1; i <= n; ++i) {
        curr[0] = i;
        char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i - 1])));
        for (size_t j = 1; j <= m; ++j) {
            char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j - 1])));
            size_t cost = (ca == cb) ? 0 : 1;
            curr[j] = std::min({
                prev[j] + 1,
                curr[j - 1] + 1,
                prev[j - 1] + cost,
            });
        }
        prev.swap(curr);
    }

    return prev[m];
}

Vector<String> suggestion_candidates(StringView needle, const Vector<StringView>& candidates)
{
    if (needle.empty() || candidates.empty()) { return {}; }

    struct Suggestion {
        StringView name;
        size_t score = 0;
    };

    Vector<Suggestion> suggestions;
    size_t max_dist = std::max<size_t>(2, needle.size() / 2);

    for (auto candidate : candidates) {
        if (candidate.empty() || candidate == needle) {
            continue;
        }
        size_t dist = edit_distance(needle, candidate);
        if (dist <= max_dist) {
            suggestions.push_back({candidate, dist});
        }
    }

    std::sort(suggestions.begin(), suggestions.end(), [](const Suggestion& a, const Suggestion& b) {
        if (a.score != b.score) {
            return a.score < b.score;
        }
        return a.name.size() < b.name.size();
    });

    Vector<String> result;
    size_t limit = std::min<size_t>(3, suggestions.size());
    result.reserve(limit);
    for (size_t i = 0; i < limit; ++i) {
        result.emplace_back(suggestions[i].name);
    }
    return result;
}

String format_suggestions(StringView needle, const Vector<StringView>& candidates)
{
    auto ranked = suggestion_candidates(needle, candidates);
    if (ranked.empty()) { return {}; }

    String result = " Did you mean ";
    for (size_t i = 0; i < ranked.size(); ++i) {
        if (i > 0) {
            result += (i == ranked.size() - 1) ? " or " : ", ";
        }
        result += fmt::format("'{}'", ranked[i]);
    }
    result += "?";
    return result;
}

} // namespace nw::smalls
