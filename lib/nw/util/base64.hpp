#pragma once

#include "ByteArray.hpp"

#include <absl/types/span.h>

namespace nw {

/// Convert base64 string to an array of bytes
ByteArray from_base64(const std::string& string);

/// Convert span of bytes to a base64 string
std::string to_base64(absl::Span<uint8_t> bytes);

} // namespace nw
