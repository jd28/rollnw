#include "lsp_uri.hpp"

#include <nw/util/string.hpp>

#include <charconv>
#include <cstdint>

namespace smalls_lsp {

namespace {

/// Decodes percent escapes. Fails on a truncated or non-hex escape rather than
/// passing the `%` through, so a malformed URI cannot silently become a path.
std::optional<std::string> percent_decode(std::string_view text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            result.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size()) { return std::nullopt; }

        uint8_t byte = 0;
        const char* first = text.data() + i + 1;
        const char* last = first + 2;
        auto parsed = std::from_chars(first, last, byte, 16);
        // Both digits must be consumed; from_chars would happily stop early on
        // something like "%2z".
        if (parsed.ec != std::errc{} || parsed.ptr != last) { return std::nullopt; }

        result.push_back(static_cast<char>(byte));
        i += 2;
    }
    return result;
}

bool is_unreserved(unsigned char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '-' || c == '.' || c == '_' || c == '~';
}

void append_encoded(std::string& out, std::string_view text, bool keep_slash)
{
    constexpr char digits[] = "0123456789ABCDEF";
    for (char raw : text) {
        auto c = static_cast<unsigned char>(raw);
        if (is_unreserved(c) || (keep_slash && c == '/')) {
            out.push_back(raw);
        } else {
            out.push_back('%');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 0x0F]);
        }
    }
}

bool is_drive_letter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// True when `text` begins with a Windows drive specifier such as `C:` or the
/// legacy URI form `C|`.
bool starts_with_drive(std::string_view text) noexcept
{
    return text.size() >= 2 && is_drive_letter(text[0])
        && (text[1] == ':' || text[1] == '|');
}

} // namespace

std::optional<std::string> uri_to_native_path(std::string_view uri, PathStyle style)
{
    size_t scheme_end = uri.find(':');
    if (scheme_end == std::string_view::npos
        || !nw::string::icmp(uri.substr(0, scheme_end), "file")) {
        return std::nullopt;
    }

    std::string_view rest = uri.substr(scheme_end + 1);

    // A fragment or query terminates the path. A `#` inside a file name has to
    // arrive as %23.
    if (size_t cut = rest.find_first_of("?#"); cut != std::string_view::npos) {
        rest = rest.substr(0, cut);
    }

    std::string_view authority;
    std::string_view path;

    if (rest.starts_with("//")) {
        std::string_view after = rest.substr(2);
        size_t path_start = after.find('/');
        if (path_start == std::string_view::npos) {
            authority = after;
            path = {};
        } else {
            authority = after.substr(0, path_start);
            path = after.substr(path_start);
        }
    } else if (rest.starts_with("/")) {
        // RFC 8089 minimal form, file:/path.
        path = rest;
    } else {
        // A relative reference does not denote a file.
        return std::nullopt;
    }

    auto decoded_authority = percent_decode(authority);
    auto decoded_path = percent_decode(path);
    if (!decoded_authority || !decoded_path) {
        return std::nullopt;
    }
    if (decoded_authority->find('\0') != std::string::npos
        || decoded_path->find('\0') != std::string::npos) {
        return std::nullopt;
    }

    bool local = decoded_authority->empty() || nw::string::icmp(*decoded_authority, "localhost");

    if (style == PathStyle::posix) {
        if (!local) {
            // No POSIX spelling for a remote authority.
            return std::nullopt;
        }
        if (decoded_path->empty()) {
            return std::nullopt;
        }
        return decoded_path;
    }

    std::string result;
    if (!local) {
        result = "\\\\" + *decoded_authority;
        result += *decoded_path;
    } else {
        std::string_view body{*decoded_path};
        // file:///C:/x carries a leading slash before the drive letter.
        if (body.size() >= 3 && body[0] == '/' && starts_with_drive(body.substr(1))) {
            body.remove_prefix(1);
        }
        if (body.empty()) {
            return std::nullopt;
        }
        result.assign(body);
        if (starts_with_drive(result) && result[1] == '|') {
            result[1] = ':';
        }
    }

    for (char& c : result) {
        if (c == '/') { c = '\\'; }
    }
    return result;
}

std::string native_path_to_uri(std::string_view path, PathStyle style)
{
    if (style == PathStyle::posix) {
        std::string result = "file://";
        if (!path.starts_with("/")) {
            result.push_back('/');
        }
        append_encoded(result, path, true);
        return result;
    }

    std::string normalized{path};
    for (char& c : normalized) {
        if (c == '\\') { c = '/'; }
    }

    std::string result = "file://";
    std::string_view body{normalized};

    if (body.starts_with("//")) {
        // UNC: the host becomes the URI authority.
        std::string_view after = body.substr(2);
        size_t share_start = after.find('/');
        std::string_view host = after.substr(0, share_start);
        append_encoded(result, host, false);
        if (share_start == std::string_view::npos) {
            result.push_back('/');
            return result;
        }
        append_encoded(result, after.substr(share_start), true);
        return result;
    }

    if (!body.starts_with("/")) {
        // A drive path needs the leading slash that file:///C:/... requires.
        result.push_back('/');
    }
    append_encoded(result, body, true);
    return result;
}

} // namespace smalls_lsp
