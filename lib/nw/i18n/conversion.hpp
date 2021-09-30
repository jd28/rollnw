#pragma once

#include "Language.hpp"

#include <string>
#include <string_view>

namespace nw {

/// Convert from utf8
std::string from_utf8(std::string_view str, std::string_view encoding, bool ignore_errors = false);
std::string from_utf8_by_langid(std::string_view str, Language::ID id, bool ignore_errors = false);

/// Convert to utf8
std::string to_utf8(std::string_view str, std::string_view encoding, bool ignore_errors = false);
std::string to_utf8_by_langid(std::string_view str, Language::ID id, bool ignore_errors = false);

} // namespace nw
