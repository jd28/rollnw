#pragma once

#include <optional>
#include <regex>
#include <string>
#include <vector>

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
std::optional<T> from(std::string_view str) = delete;

/// @cond NEVER
template <>
std::optional<bool> from(std::string_view str);
template <>
std::optional<int32_t> from(std::string_view str);
template <>
std::optional<uint32_t> from(std::string_view str);
template <>
std::optional<int64_t> from(std::string_view str);
template <>
std::optional<uint64_t> from(std::string_view str);
template <>
std::optional<float> from(std::string_view str);
template <>
std::optional<double> from(std::string_view str);
/// @endcond

/**
 * @brief Trim left in place
 */
std::string* ltrim_in_place(std::string* str);

/**
 * @brief Trims right in place
 */
std::string* rtrim_in_place(std::string* str);

/**
 * @brief Trims string in place
 */
std::string* trim_in_place(std::string* str);

// Join & Split...

/**
 * @brief Joins a vector of strings.
 *
 * @param strings Vector of strings.
 * @param delim Separator.  Default " "
 * @return std::string
 */
std::string join(const std::vector<std::string>& strings, const char* delim = " ");

/**
 * @brief Splits a string into an vector of strings
 *
 * @param str String to split
 * @param delim Delimiter
 * @param skipEmpty Ignore empty strings
 * @param trimmed Trim strings after split
 * @return std::vector<std::string>
 */
std::vector<std::string> split(const std::string& str, char delim, bool skipEmpty = true, bool trimmed = true);

/**
 * @brief Case insenstive comparison
 */
bool icmp(std::string_view first, std::string_view second);

/**
 * @brief Converts string to lowercase
 *
 * @param str String to be modified
 */
void tolower(std::string* str);

/**
 * @brief Determines if a string starts with a given prefix
 *
 * @param str String to chect
 * @param prefix Prefix
 * @return bool
 */
bool startswith(std::string_view str, std::string_view prefix);

/**
 * @brief Determines if a string ends with a given suffix
 *
 * @param str String to check
 * @param suffix
 * @return bool
 */
bool endswith(std::string_view str, std::string_view suffix);

/**
 * @brief Converts a glob pattern to a regex
 * @note This only supports ``?``, ``*``, and ``[seq]``
 * @param pattern E.g, "file?_nam*.ext"
 * @param icase If true returns a case insentive regex
 * @return std::regex
 */
std::regex glob_to_regex(std::string_view pattern, bool icase = false);

} // namespace nw::string
