#include "Diagnostic.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace nw::smalls {

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

String format_suggestions(StringView needle, const Vector<StringView>& candidates)
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

    if (suggestions.empty()) {
        return {};
    }

    std::sort(suggestions.begin(), suggestions.end(), [](const Suggestion& a, const Suggestion& b) {
        if (a.score != b.score) {
            return a.score < b.score;
        }
        return a.name.size() < b.name.size();
    });

    size_t limit = std::min<size_t>(3, suggestions.size());
    String result = " Did you mean ";
    for (size_t i = 0; i < limit; ++i) {
        if (i > 0) {
            result += (i == limit - 1) ? " or " : ", ";
        }
        result += fmt::format("'{}'", suggestions[i].name);
    }
    result += "?";
    return result;
}

} // namespace nw::smalls
