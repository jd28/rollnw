#include "string.hpp"

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
#include <strings.h>

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
        type v;                                                                        \
        const char* start = str.data() + (b == 16 ? 2 : 0);                            \
        auto res = std::from_chars(start, str.data() + str.size(), v, b);              \
        return res.ptr != str.data() ? std::optional<type>(v) : std::optional<type>(); \
    }

#define DEFINE_FROM_FLOAT(type)                                                        \
    template <>                                                                        \
    std::optional<type> from(std::string_view str)                                     \
    {                                                                                  \
        type v;                                                                        \
        auto res = absl::from_chars(str.data(), str.data() + str.size(), v);           \
        return res.ptr != str.data() ? std::optional<type>(v) : std::optional<type>(); \
    }

DEFINE_FROM_INT(int32_t)
DEFINE_FROM_INT(uint32_t)
DEFINE_FROM_INT(int64_t)
DEFINE_FROM_INT(uint64_t)
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
    return icase ? std::regex(s, std::regex::icase) : std::regex(s);
}

} // namespace nw::string
