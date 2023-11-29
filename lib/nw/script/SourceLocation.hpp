#pragma once

#include <string_view>
#include <tuple>

namespace nw::script {

/// Position in source code
struct SourcePosition {
    size_t line = 0;   ///< Starting line
    size_t column = 0; ///< Starting column

    bool operator==(const SourcePosition& rhs) const = default;
};

inline bool operator<(const SourcePosition lhs, const SourcePosition rhs)
{
    return std::tie(lhs.line, lhs.column) < std::tie(rhs.line, rhs.column);
}

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
    std::string_view view() const noexcept
    {
        if (!start || !end) { return {}; }
        return std::string_view{start, length()};
    }
};

inline bool contains_position(SourceRange haystack, SourcePosition needle)
{
    return std::tie(haystack.start.line, haystack.start.column) <= std::tie(needle.line, needle.column)
        && std::tie(haystack.end.line, haystack.end.column) >= std::tie(needle.line, needle.column);
}

} // namespace nw::script
