#pragma once

#include "ByteArray.hpp"

#include <absl/types/span.h>

namespace nw {

/// Converts base64 string to an array of bytes
ByteArray from_base64(const std::string& string);

/// Converts span of bytes to a base64 string
std::string to_base64(absl::Span<const uint8_t> bytes);

} // namespace nw
