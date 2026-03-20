#pragma once

#include <cctype>
#include <string>

/// Returns the identifier that contains column `col` on line `line` of `text`.
/// Returns empty string if the position is not on an identifier character.
inline std::string identifier_at(const std::string& text, int line, int col)
{
    size_t pos = 0;
    for (int i = 0; i < line && pos < text.size(); ++i) {
        pos = text.find('\n', pos);
        if (pos == std::string::npos) return {};
        ++pos;
    }
    if (pos + col >= text.size()) return {};
    if (!std::isalnum(static_cast<unsigned char>(text[pos + col])) && text[pos + col] != '_')
        return {};

    int start = col;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[pos + start - 1]))
               || text[pos + start - 1] == '_')) {
        --start;
    }
    int end = col;
    while (pos + end < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos + end]))
               || text[pos + end] == '_')) {
        ++end;
    }
    if (start == end) return {};
    return text.substr(pos + start, end - start);
}

/// Returns the identifier immediately before a dot at column `dot_col - 1` on `line`.
/// `dot_col` should be the column right after the dot (i.e., where completion triggers).
inline std::string identifier_before(const std::string& text, int line, int dot_col)
{
    size_t pos = 0;
    for (int i = 0; i < line && pos < text.size(); ++i) {
        pos = text.find('\n', pos);
        if (pos == std::string::npos) return {};
        ++pos;
    }
    int end = dot_col - 1; // exclusive end of identifier = the dot's column
    if (end <= 0) return {};
    // Character immediately before the dot must be an identifier char
    if (!std::isalnum(static_cast<unsigned char>(text[pos + end - 1])) && text[pos + end - 1] != '_')
        return {};
    int start = end - 1;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[pos + start - 1]))
               || text[pos + start - 1] == '_')) {
        --start;
    }
    if (start == end) return {};
    return text.substr(pos + start, end - start);
}

/// Given a buffer, line, and cursor column, determines if the cursor is in a
/// dot-completion context (i.e., there is a dot immediately before the current
/// word). Returns true and sets `dot_col` to the column right after the dot.
inline bool detect_dot_trigger(const std::string& text, int line, int character, int& dot_col)
{
    size_t line_start = 0;
    for (int i = 0; i < line && line_start < text.size(); ++i) {
        auto nl = text.find('\n', line_start);
        if (nl == std::string::npos) break;
        line_start = nl + 1;
    }
    // Scan left past any partial identifier the user has typed after the dot
    int scan = character;
    while (scan > 0) {
        char c = text[line_start + scan - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') { --scan; }
        else { break; }
    }
    if (scan > 0 && line_start + scan - 1 < text.size()
        && text[line_start + scan - 1] == '.') {
        dot_col = scan;
        return true;
    }
    return false;
}
