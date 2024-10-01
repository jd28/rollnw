#pragma once

#include "ByteArray.hpp"

#include <span>

namespace nw {

/// Converts base64 string to an array of bytes
ByteArray from_base64(const String& string);

/// Converts span of bytes to a base64 string
String to_base64(std::span<const uint8_t> bytes);

} // namespace nw
