#pragma once

#include "../config.hpp"

#include <tuple>

namespace nw::script {

/// Position in source code
struct SourcePosition {
    size_t line = 0;   ///< Starting line
    size_t column = 0; ///< Starting column

    bool operator==(const SourcePosition& rhs) const = default;
    auto operator<=>(const SourcePosition& rhs) const = default;
};

/// Range of source code
struct SourceRange {
    SourcePosition start; ///< Start of range
    SourcePosition end;   ///< End of Range
};

// Location in source code
struct SourceLocation {
    const char* start = nullptr; ///< Pointer to start of source code
    const char* end = nullptr;   ///< Pointer to end of source code
    SourceRange range;           ///< Source range

    /// Gets the length of source code covered
    size_t length() const noexcept
    {
        if (!start || !end) { return {}; }
        return size_t(end - start);
    }

    /// Gets a view of the source code covered
    StringView view() const noexcept
    {
        if (!start || !end) { return {}; }
        return StringView{start, length()};
    }
};

inline bool contains_position(SourceRange haystack, SourcePosition needle)
{
    return std::tie(haystack.start.line, haystack.start.column) <= std::tie(needle.line, needle.column)
        && std::tie(haystack.end.line, haystack.end.column) >= std::tie(needle.line, needle.column);
}

inline bool contains_range(SourceRange haystack, SourceRange needle)
{
    return contains_position(haystack, needle.start) && contains_position(haystack, needle.end);
}

} // namespace nw::script
