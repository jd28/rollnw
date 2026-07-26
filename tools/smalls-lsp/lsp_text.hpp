#pragma once

#include <nowide/utf/utf.hpp>

#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

enum class PositionEncoding {
    utf8,
    utf16,
};

/// Returns the byte bounds [start, end) of a zero-based line.
inline std::optional<std::pair<size_t, size_t>> line_bounds(std::string_view text, int line)
{
    if (line < 0) {
        return std::nullopt;
    }

    size_t start = 0;
    for (int i = 0; i < line; ++i) {
        size_t newline = text.find('\n', start);
        if (newline == std::string::npos) {
            return std::nullopt;
        }
        start = newline + 1;
    }

    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
        end = text.size();
    }
    if (end > start && text[end - 1] == '\r') {
        --end;
    }
    return std::pair{start, end};
}

/// Returns true when a zero-based byte position is on or at the end of a line.
inline bool valid_text_position(std::string_view text, int line, int character)
{
    auto bounds = line_bounds(text, line);
    return bounds && character >= 0
        && static_cast<size_t>(character) <= bounds->second - bounds->first;
}

inline std::optional<int> utf8_prefix_units(
    std::string_view text, size_t byte_count, PositionEncoding encoding)
{
    if (byte_count > text.size()) {
        return std::nullopt;
    }

    auto current = text.begin();
    auto target = current + byte_count;
    int units = 0;
    while (current != target) {
        auto previous = current;
        auto codepoint = nowide::utf::utf_traits<char>::decode(current, text.end());
        if (codepoint == nowide::utf::illegal || codepoint == nowide::utf::incomplete
            || current > target) {
            return std::nullopt;
        }

        auto width = encoding == PositionEncoding::utf8
            ? current - previous
            : nowide::utf::utf_traits<char16_t>::width(codepoint);
        if (width > std::numeric_limits<int>::max() - units) {
            return std::nullopt;
        }
        units += static_cast<int>(width);
    }
    return units;
}

/// Converts an LSP character offset on a line to a UTF-8 byte column.
inline std::optional<int> lsp_character_to_byte(
    std::string_view text, int line, int character, PositionEncoding encoding)
{
    auto bounds = line_bounds(text, line);
    if (!bounds || character < 0) {
        return std::nullopt;
    }

    std::string_view line_text = text.substr(bounds->first, bounds->second - bounds->first);
    if (encoding == PositionEncoding::utf8) {
        size_t byte = static_cast<size_t>(character);
        if (!utf8_prefix_units(line_text, byte, encoding)) {
            return std::nullopt;
        }
        return character;
    }

    auto current = line_text.begin();
    int units = 0;
    while (current != line_text.end()) {
        if (units == character) {
            if (current - line_text.begin() > std::numeric_limits<int>::max()) {
                return std::nullopt;
            }
            return static_cast<int>(current - line_text.begin());
        }

        auto codepoint = nowide::utf::utf_traits<char>::decode(current, line_text.end());
        if (codepoint == nowide::utf::illegal || codepoint == nowide::utf::incomplete) {
            return std::nullopt;
        }

        int width = nowide::utf::utf_traits<char16_t>::width(codepoint);
        if (units > std::numeric_limits<int>::max() - width
            || units + width > character) {
            return std::nullopt;
        }
        units += width;
    }

    if (units == character) {
        if (line_text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(line_text.size());
    }
    return std::nullopt;
}

/// Converts a UTF-8 byte column on a line to an LSP character offset.
inline std::optional<int> byte_character_to_lsp(
    std::string_view text, int line, int byte_character, PositionEncoding encoding)
{
    auto bounds = line_bounds(text, line);
    if (!bounds || byte_character < 0) {
        return std::nullopt;
    }

    std::string_view line_text = text.substr(bounds->first, bounds->second - bounds->first);
    size_t byte = static_cast<size_t>(byte_character);
    return utf8_prefix_units(line_text, byte, encoding);
}

/// True for characters the Smalls lexer accepts inside an identifier.
/// `$` leads a generic parameter and is accepted by `Lexer::next`, so the
/// editor-side word boundary must accept it too or hover, definition, and
/// completion disagree with the lexer about where a name starts.
inline bool is_identifier_char(char c) noexcept
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

/// Converts an LSP position to an absolute byte offset into `text`.
inline std::optional<size_t> lsp_position_to_offset(
    std::string_view text, int line, int character, PositionEncoding encoding)
{
    auto bounds = line_bounds(text, line);
    if (!bounds) {
        return std::nullopt;
    }
    auto byte = lsp_character_to_byte(text, line, character, encoding);
    if (!byte) {
        return std::nullopt;
    }
    return bounds->first + static_cast<size_t>(*byte);
}

/// Applies one incremental content change in place.
///
/// Returns false and leaves `text` untouched when the range is unusable, so a
/// rejected change cannot leave a partially edited buffer behind.
inline bool apply_content_change(std::string& text, int start_line, int start_character,
    int end_line, int end_character, std::string_view replacement,
    PositionEncoding encoding)
{
    auto start = lsp_position_to_offset(text, start_line, start_character, encoding);
    auto end = lsp_position_to_offset(text, end_line, end_character, encoding);
    if (!start || !end || *start > *end || *end > text.size()) {
        return false;
    }
    text.replace(*start, *end - *start, replacement);
    return true;
}

/// Returns the identifier that contains column `col` on line `line` of `text`.
/// Returns empty string if the position is not on an identifier character.
inline std::string identifier_at(std::string_view text, int line, int col)
{
    auto bounds = line_bounds(text, line);
    if (!bounds || col < 0 || static_cast<size_t>(col) >= bounds->second - bounds->first) {
        return {};
    }

    size_t pos = bounds->first;
    if (!is_identifier_char(text[pos + col])) {
        return {};
    }

    int start = col;
    while (start > 0 && is_identifier_char(text[pos + start - 1])) {
        --start;
    }
    int end = col;
    while (pos + end < bounds->second && is_identifier_char(text[pos + end])) {
        ++end;
    }
    if (start == end) return {};
    return std::string{text.substr(pos + start, end - start)};
}

/// Returns the identifier immediately before a dot at column `dot_col - 1` on `line`.
/// `dot_col` should be the column right after the dot (i.e., where completion triggers).
inline std::string identifier_before(std::string_view text, int line, int dot_col)
{
    auto bounds = line_bounds(text, line);
    if (!bounds || dot_col <= 0
        || static_cast<size_t>(dot_col) > bounds->second - bounds->first
        || text[bounds->first + dot_col - 1] != '.') {
        return {};
    }

    size_t pos = bounds->first;
    int end = dot_col - 1; // exclusive end of identifier = the dot's column
    if (end <= 0) return {};
    // Character immediately before the dot must be an identifier char
    if (!is_identifier_char(text[pos + end - 1]))
        return {};
    int start = end - 1;
    while (start > 0 && is_identifier_char(text[pos + start - 1])) {
        --start;
    }
    if (start == end) return {};
    return std::string{text.substr(pos + start, end - start)};
}

/// Given a buffer, line, and cursor column, determines if the cursor is in a
/// dot-completion context (i.e., there is a dot immediately before the current
/// word). Returns true and sets `dot_col` to the column right after the dot.
inline bool detect_dot_trigger(std::string_view text, int line, int character, int& dot_col)
{
    auto bounds = line_bounds(text, line);
    if (!bounds || character < 0
        || static_cast<size_t>(character) > bounds->second - bounds->first) {
        return false;
    }

    size_t line_start = bounds->first;
    // Scan left past any partial identifier the user has typed after the dot
    int scan = character;
    while (scan > 0) {
        if (is_identifier_char(text[line_start + scan - 1])) {
            --scan;
        } else {
            break;
        }
    }
    if (scan > 0 && line_start + scan - 1 < text.size()
        && text[line_start + scan - 1] == '.') {
        dot_col = scan;
        return true;
    }
    return false;
}
