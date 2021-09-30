#include "conversion.hpp"

#include <iconv.h>

#include <stdexcept>

namespace nw {

namespace detail {
inline std::string iconv_wrapper(std::string_view str, const char* from, const char* to, bool ignore_errors)
{
    std::string s;
    iconv_t conv = iconv_open(to, from);
    char* src_ptr = (char*)str.data();
    size_t src_size = str.size();
    char dst_buffer[2024];

    while (0 < src_size) {
        char* dst_ptr = dst_buffer;
        size_t dst_size = 2024;
        size_t res = ::iconv(conv, &src_ptr, &src_size, &dst_ptr, &dst_size);
        if (res == (size_t)-1) {
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

std::string from_utf8_by_langid(std::string_view str, Language::ID id, bool ignore_errors)
{
    return from_utf8(str, Language::encoding(id), ignore_errors);
}

std::string from_utf8(std::string_view str, std::string_view encoding, bool ignore_errors)
{
    return detail::iconv_wrapper(str, encoding.data(), "UTF-8", ignore_errors);
}

std::string to_utf8_by_langid(std::string_view str, Language::ID id, bool ignore_errors)
{
    return to_utf8(str, Language::encoding(id), ignore_errors);
}

std::string to_utf8(std::string_view str, std::string_view encoding, bool ignore_errors)
{
    return detail::iconv_wrapper(str, "UTF-8", encoding.data(), ignore_errors);
}

} // namespace nw
