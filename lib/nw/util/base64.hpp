#pragma once

#include "ByteArray.hpp"

#include <absl/types/span.h>

namespace nw {

ByteArray from_base64(const std::string& string);
std::string to_base64(absl::Span<uint8_t> bytes);

} // namespace nw
