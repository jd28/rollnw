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

constexpr int base(StringView str)
{
    if (str.length() > 2 && str[0] == '0') {
        switch (str[1]) {
        case 'x':
        case 'X':
            return 16;
        case 'b':
        case 'B':
            return 2;
        case 'o':
        case 'O':
            return 8;
        }
    }
    return 10;
}

} // namespace detail

template <>
std::optional<bool> from(StringView str)
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
    std::optional<type> from(StringView str)                                           \
    {                                                                                  \
        int b = detail::base(str);                                                     \
        type v = 0;                                                                    \
        const char* start = str.data() + (b != 10 ? 2 : 0);                            \
        auto res = std::from_chars(start, str.data() + str.size(), v, b);              \
        return res.ptr != str.data() ? std::optional<type>(v) : std::optional<type>(); \
    }

#define DEFINE_FROM_FLOAT(type)                                                        \
    template <>                                                                        \
    std::optional<type> from(StringView str)                                           \
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

String* ltrim_in_place(String* str)
{
    str->erase(0, str->find_first_not_of(" \n\r\t"));
    return str;
}

String* rtrim_in_place(String* str)
{
    str->erase(str->find_last_not_of(" \n\r\t") + 1);
    return str;
}

String* trim_in_place(String* str)
{
    return ltrim_in_place(rtrim_in_place(str));
}

String join(const Vector<String>& strings, const char* delim)
{
    if (strings.empty()) return {};
    return absl::StrJoin(strings, delim);
}

Vector<String> split(const String& sp, char delim, bool skipEmpty, bool trimmed)
{
    Vector<String> v;

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

bool icmp(StringView first, StringView second)
{
    if (first.size() != second.size())
        return false;

    return strncasecmp(first.data(), second.data(), first.size()) == 0;
}

void tolower(String* str)
{
    std::transform(str->begin(), str->end(), str->begin(), ::tolower);
}

bool startswith(StringView str, StringView prefix)
{
    return absl::StartsWith({str.data(), str.size()}, {prefix.data(), prefix.size()});
}

bool endswith(StringView str, StringView suffix)
{
    return absl::EndsWith({str.data(), str.size()}, {suffix.data(), suffix.size()});
}

std::regex glob_to_regex(StringView pattern, bool icase)
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

String sanitize_colors(String str)
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

String desanitize_colors(String str)
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
