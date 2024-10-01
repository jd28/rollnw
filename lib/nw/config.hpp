#pragma once

// This file is inspired by RmlUI config header.  The goal is to have a file that can parameterize
// the library by a set types.

#ifdef ROLLNW_CUSTOM_CONFIGURATION_FILE
#include ROLLNW_CUSTOM_CONFIGURATION_FILE
#else

#include <string>

namespace nw {

// Strings.
using String = std::string;
using StringView = std::string_view;

} // namespace nw

#endif
