#pragma once

#include "Language.hpp"

#include <string>
#include <string_view>

namespace nw {

/// Convert from utf8
String from_utf8(StringView str, StringView encoding, bool ignore_errors = false);
String from_utf8_by_langid(StringView str, LanguageID id, bool ignore_errors = false);
String from_utf8_by_global_lang(StringView str, bool ignore_errors = false);

/// Convert to utf8
String to_utf8(StringView str, StringView encoding, bool ignore_errors = false);
String to_utf8_by_langid(StringView str, LanguageID id, bool ignore_errors = false);
String to_utf8_by_global_lang(StringView str, bool ignore_errors = false);

} // namespace nw
