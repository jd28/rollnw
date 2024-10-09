#pragma once

// This file is inspired by RmlUI config header.  The goal is to have a file that can parameterize
// the library by a set types.

#ifdef ROLLNW_CUSTOM_CONFIGURATION_FILE
#include ROLLNW_CUSTOM_CONFIGURATION_FILE
#else

#include "util/memory.hpp"

#include <string>
#include <vector>

namespace nw {

// Strings.
using String = std::string;
using StringView = std::string_view;
using PoolString = std::basic_string<char, std::char_traits<char>, PoolAllocator<char>>;
using ScopeString = std::basic_string<char, std::char_traits<char>, ScopeAllocator<char>>;

// Vector
template <typename T>
using Vector = std::vector<T>;
template <typename T>
using ScopeVector = std::vector<T, ScopeAllocator<T>>;

} // namespace nw

#endif
