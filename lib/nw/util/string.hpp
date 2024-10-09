#pragma once

#include "../config.hpp"

#include <optional>
#include <regex>
#include <string>

/// namespace for string related functions
namespace nw::string {

/**
 * @brief String conversions to integral and floating pointing types
 *
 * @note Even those tho this function is deleted, see Template Parameters for specilized versions.
 * @tparam T ``bool``, ``int32_t``, ``uint32_t``, ``int64_t``, ``uint64_t``, ``float``, ``double``
 * @param str Input string
 * @return std::optional<T>
 */
template <typename T>
std::optional<T> from(StringView str) = delete;

/// @cond NEVER
template <>
std::optional<bool> from(StringView str);
template <>
std::optional<int16_t> from(StringView str);
template <>
std::optional<int32_t> from(StringView str);
template <>
std::optional<uint32_t> from(StringView str);
template <>
std::optional<int64_t> from(StringView str);
#ifdef ROLLNW_OS_MACOS
template <>
std::optional<uint64_t> from(StringView str);
#endif
template <>
std::optional<size_t> from(StringView str);
template <>
std::optional<float> from(StringView str);
template <>
std::optional<double> from(StringView str);
/// @endcond

/// Trims left in place
String* ltrim_in_place(String* str);

/// Trims right in place
String* rtrim_in_place(String* str);

/// Trims string in place
String* trim_in_place(String* str);

// Join & Split...

/**
 * @brief Joins a vector of strings.
 *
 * @param strings Vector of strings.
 * @param delim Separator.  Default " "
 * @return String
 */
String join(const Vector<String>& strings, const char* delim = " ");

/**
 * @brief Splits a string into an vector of strings
 *
 * @param str String to split
 * @param delim Delimiter
 * @param skipEmpty Ignore empty strings
 * @param trimmed Trim strings after split
 * @return Vector<String>
 */
Vector<String> split(const String& str, char delim, bool skipEmpty = true, bool trimmed = true);

/// Case insenstive comparison
bool icmp(StringView first, StringView second);

/// Converts string to lowercase, in place
void tolower(String* str);

/// Determines if a string starts with a given prefix
bool startswith(StringView str, StringView prefix);

/// Determines if a string ends with a given suffix
bool endswith(StringView str, StringView suffix);

/**
 * @brief Converts a glob pattern to a regex
 * @note This only supports ``?``, ``*``, and ``[seq]``
 * @param pattern E.g, "file?_nam*.ext"
 * @param icase If true returns a case insentive regex
 * @return std::regex
 */
std::regex glob_to_regex(StringView pattern, bool icase = false);

/// Converts color bytes to hex \<c\\x\\x\\x\> -> \<cXXXXXX\>.  Note: MOVE in the string.
String sanitize_colors(String str);

/// Converts color hex to bytes \<cXXXXXX\> -> \<c\\x\\x\\x\>.  Note: MOVE in the string.
String desanitize_colors(String str);

} // namespace nw::string
