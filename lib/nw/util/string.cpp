#include "string.hpp"

#include "../log.hpp"

#include <absl/strings/charconv.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_replace.h>
#include <absl/strings/str_split.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <optional>
#include <sstream>
#include <string_view>

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

namespace nw::string {

/// @cond NEVER
namespace detail {

constexpr int base(std::string_view str)
{
    return str.length() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X') ? 16 : 10;
}

} // namespace detail

template <>
std::optional<bool> from(std::string_view str) ///< @private
{
    for (const auto& t : {"t", "true", "y", "yes", "1"}) {
        if (icmp(t, str)) {
            return std::make_optional<bool>(true);
        }
    }

    for (const auto& f : {"f", "false", "n", "no", "0"}) {
        if (icmp(f, str)) {
            return std::make_optional<bool>(false);
        }
    }

    return std::optional<bool>();
}

#define DEFINE_FROM_INT(type)                                                          \
    template <>                                                                        \
    std::optional<type> from(std::string_view str)                                     \
    {                                                                                  \
        int b = detail::base(str);                                                     \
        type v = 0;                                                                    \
        const char* start = str.data() + (b == 16 ? 2 : 0);                            \
        auto res = std::from_chars(start, str.data() + str.size(), v, b);              \
        return res.ptr != str.data() ? std::optional<type>(v) : std::optional<type>(); \
    }

#define DEFINE_FROM_FLOAT(type)                                                        \
    template <>                                                                        \
    std::optional<type> from(std::string_view str)                                     \
    {                                                                                  \
        type v = 0.0;                                                                  \
        auto res = absl::from_chars(str.data(), str.data() + str.size(), v);           \
        return res.ptr != str.data() ? std::optional<type>(v) : std::optional<type>(); \
    }

DEFINE_FROM_INT(int16_t)
DEFINE_FROM_INT(int32_t)
DEFINE_FROM_INT(uint32_t)
DEFINE_FROM_INT(int64_t)
#ifdef ROLLNW_OS_MACOS
DEFINE_FROM_INT(uint64_t)
#endif
DEFINE_FROM_INT(size_t)
DEFINE_FROM_FLOAT(float)
DEFINE_FROM_FLOAT(double)

#undef DEFINE_FROM_INT
#undef DEFINE_FROM_FLOAT

/// @endcond

std::string* ltrim_in_place(std::string* str)
{
    str->erase(0, str->find_first_not_of(" \n\r\t"));
    return str;
}

std::string* rtrim_in_place(std::string* str)
{
    str->erase(str->find_last_not_of(" \n\r\t") + 1);
    return str;
}

std::string* trim_in_place(std::string* str)
{
    return ltrim_in_place(rtrim_in_place(str));
}

std::string join(const std::vector<std::string>& strings, const char* delim)
{
    if (strings.empty()) return {};
    return absl::StrJoin(strings, delim);
}

std::vector<std::string> split(const std::string& sp, char delim, bool skipEmpty, bool trimmed)
{
    std::vector<std::string> v;

    if (skipEmpty)
        v = absl::StrSplit(sp, delim, absl::SkipWhitespace());
    else
        v = absl::StrSplit(sp, delim, absl::AllowEmpty());

    if (trimmed) {
        for (auto& s : v) {
            trim_in_place(&s);
        }
    }

    return v;
}

bool icmp(std::string_view first, std::string_view second)
{
    if (first.size() != second.size())
        return false;

    return strncasecmp(first.data(), second.data(), first.size()) == 0;
}

void tolower(std::string* str)
{
    std::transform(str->begin(), str->end(), str->begin(), ::tolower);
}

bool startswith(std::string_view str, std::string_view prefix)
{
    return absl::StartsWith({str.data(), str.size()}, {prefix.data(), prefix.size()});
}

bool endswith(std::string_view str, std::string_view suffix)
{
    return absl::EndsWith({str.data(), str.size()}, {suffix.data(), suffix.size()});
}

std::regex glob_to_regex(std::string_view pattern, bool icase)
{
    // Replace all regex special characters
    auto s = absl::StrReplaceAll({pattern.data(), pattern.size()},
        {{"\\", "\\\\"}, {"^", "\\^"}, {".", "\\."}, {"$", "\\$"},
            {"|", "\\|"}, {"(", "\\("}, {")", "\\)"}, {"*", "\\*"},
            {"+", "\\+"}, {"?", "\\?"}, {"/", "\\/"}});
    // Put back glob patterns.
    absl::StrReplaceAll({{"\\?", "."}, {"\\*", ".*"}}, &s);
    try {
        return icase ? std::regex(s, std::regex::icase) : std::regex(s);
    } catch (const std::regex_error&) {
        LOG_F(ERROR, "Failed to create regex from '{}' glob", pattern);
        return {};
    }
}

std::string sanitize_colors(std::string str)
{
    for (size_t i = 1; i < str.length(); ++i) {
        if (str[i - 1] == '<' && str[i] == 'c') {
            if (i + 4 >= str.length() || str[i + 4] != '>') {
                LOG_F(ERROR, "invalid color code: '{}'", str);
                continue;
            }

            auto s = fmt::format("{:02X}{:02X}{:02X}",
                static_cast<uint8_t>(str[i + 1]),
                static_cast<uint8_t>(str[i + 2]),
                static_cast<uint8_t>(str[i + 3]));

            str[i + 1] = s[0];
            str[i + 2] = s[1];
            str[i + 3] = s[2];
            str.insert(i + 4, s.data() + 3, 3);
        }
    }
    return str;
}

std::string desanitize_colors(std::string str)
{
    size_t len = str.length();
    for (size_t i = 1; i < len; ++i) {
        if (str[i - 1] != '<' || str[i] != 'c') {
            continue;
        }
        if (i + 7 >= len || str[i + 7] != '>') {
            LOG_F(ERROR, "invalid color code: '{}'", str);
            continue;
        }

        uint8_t c1, c2, c3;
        auto r1 = std::from_chars(&str[i + 1], &str[i + 3], c1, 16);
        auto r2 = std::from_chars(&str[i + 3], &str[i + 5], c2, 16);
        auto r3 = std::from_chars(&str[i + 5], &str[i + 7], c3, 16);
        if (r1.ec != std::errc() || r2.ec != std::errc() || r3.ec != std::errc()) {
            LOG_F(ERROR, "failed to desanitize color code: '{}'", str);
            continue;
        }

        str[i + 1] = static_cast<char>(c1);
        str[i + 2] = static_cast<char>(c2);
        str[i + 3] = static_cast<char>(c3);
        str.erase(std::begin(str) + ptrdiff_t(i) + 4, std::begin(str) + ptrdiff_t(i) + 7);
        len -= 3;
    }
    return str;
}

} // namespace nw::string
