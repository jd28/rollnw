#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace smalls_lsp {

/// Which filesystem convention a native path string follows.
///
/// The transform is parameterized rather than compiled per platform so the
/// Windows contract can be exercised from a POSIX test run.
enum class PathStyle {
    posix,
    windows,
};

constexpr PathStyle native_path_style =
#ifdef _WIN32
    PathStyle::windows;
#else
    PathStyle::posix;
#endif

/// Converts an RFC 8089 `file` URI to a native path.
///
/// Contract:
/// - The scheme must be `file`, compared case-insensitively. Any other scheme
///   is rejected; a path policy is never inferred from a string prefix.
/// - `//` introduces an authority. An empty authority or `localhost` means the
///   local machine. Any other authority is a UNC host: under
///   `PathStyle::windows` it becomes `\\host\share\...`, and under
///   `PathStyle::posix` it is rejected, because POSIX has no way to express it.
/// - Percent escapes must be `%` followed by two hex digits. An invalid escape
///   is rejected rather than passed through.
/// - `?` and `#` terminate the path, per RFC 3986. A file whose name contains
///   `#` must arrive percent-encoded as `%23`.
/// - A decoded NUL is rejected.
/// - Under `PathStyle::windows` a leading `/` before a drive letter is dropped,
///   the legacy `C|` form is accepted as `C:`, and `/` becomes `\`.
///
/// Returns nullopt when the URI does not denote a path under these rules.
std::optional<std::string> uri_to_native_path(
    std::string_view uri, PathStyle style = native_path_style);

/// Converts a native path to an RFC 8089 `file` URI.
///
/// Every byte outside the RFC 3986 unreserved set is percent-encoded with
/// uppercase hex, so spaces, `#`, `%`, `:`, and non-ASCII bytes all survive a
/// round trip. Path separators stay literal.
///
/// Under `PathStyle::windows`, `\` becomes `/`, a leading `\\host\share` becomes
/// the URI authority, and a drive path gains the leading `/` that
/// `file:///C:/...` requires.
std::string native_path_to_uri(
    std::string_view path, PathStyle style = native_path_style);

} // namespace smalls_lsp
