#pragma once

#include "ByteArray.hpp"

#include <span>

namespace nw {

/**
 * @brief Decompress a NWN:EE compressed buffer
 *
 * @note Doesn't support Zstd dictionaries, but the game doesn't either.. yet.
 *       Supporting that will likely lead to API change.
 *
 * @param span Compressed data
 * @param magic Magic 4 byte sequence, i.e. "NSYC"
 * @return ByteArray Uncompressed data, empty on error.
 */
ByteArray decompress(std::span<const uint8_t> span, const char* magic);

} // namespace nw
