#include "conversion.hpp"

#include "../kernel/Strings.hpp"
#include "../log.hpp"
#include "../util/scope_exit.hpp"

#include <iconv.h>

#include <limits>
#include <stdexcept>

namespace nw {

namespace detail {
inline String iconv_wrapper(StringView str, const char* from, const char* to, bool ignore_errors)
{
    String s;
    if (!from || !to) {
        LOG_F(ERROR, "invalid encoding from: {}, to: {}", from, to);
        return s;
    }
    iconv_t conv = iconv_open(to, from);
    SCOPE_EXIT([conv]() { iconv_close(conv); });

    const char* src_ptr = str.data(); // Doesn't modify str
    size_t src_size = str.size();
    char dst_buffer[2024];

    // Code from stackoverflow, will find source.
    while (0 < src_size) {
        char* dst_ptr = dst_buffer;
        size_t dst_size = 2024;

#ifdef ROLLNW_OS_WINDOWS
        size_t res = ::iconv(conv, &src_ptr, &src_size, &dst_ptr, &dst_size);
#else
        char* sp = const_cast<char*>(src_ptr);
        size_t res = ::iconv(conv, &sp, &src_size, &dst_ptr, &dst_size);
#endif
        if (res == std::numeric_limits<size_t>::max()) {
            if (errno == E2BIG) {
                // ignore this error
            } else if (ignore_errors) {
                ++src_ptr;
                --src_size;
            } else {
                switch (errno) {
                case EILSEQ:
                case EINVAL:
                    throw std::runtime_error("invalid multibyte chars");
                default:
                    throw std::runtime_error("unknown error");
                }
            }
        }
        s.append(&dst_buffer[0], 2024 - dst_size);
    }
    return s;
}
} // namespace detail

String from_utf8_by_langid(StringView str, LanguageID id, bool ignore_errors)
{
    return from_utf8(str, Language::encoding(id), ignore_errors);
}

String from_utf8(StringView str, StringView encoding, bool ignore_errors)
{
    return detail::iconv_wrapper(str, "UTF-8", encoding.data(), ignore_errors);
}

String from_utf8_by_global_lang(StringView str, bool ignore_errors)
{
    return from_utf8_by_langid(str, kernel::strings().global_language(), ignore_errors);
}

String to_utf8_by_langid(StringView str, LanguageID id, bool ignore_errors)
{
    return to_utf8(str, Language::encoding(id), ignore_errors);
}

String to_utf8(StringView str, StringView encoding, bool ignore_errors)
{
    return detail::iconv_wrapper(str, encoding.data(), "UTF-8", ignore_errors);
}

String to_utf8_by_global_lang(StringView str, bool ignore_errors)
{
    return to_utf8_by_langid(str, kernel::strings().global_language(), ignore_errors);
}

} // namespace nw
