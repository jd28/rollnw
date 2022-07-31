#include "compression.hpp"

#include "../log.hpp"

#include <zlib.h>
#include <zstd.h>

#include <array>
#include <cstring>
#include <memory>

namespace nw {

namespace detail {

void zstd_cctx_deleter(ZSTD_CCtx* ctx) { ZSTD_freeCCtx(ctx); }
void zstd_dctx_deleter(ZSTD_DCtx* ctx) { ZSTD_freeDCtx(ctx); }

} // namespace detail

// Had to crib from neverwinter.nim here for what the headers and
// of the files were.

struct CompressionHeader {
    std::array<char, 4> magic;
    uint32_t version;
    uint32_t algorithm;
    uint32_t uncompressed_size;
};

struct ZlibHeader {
    uint32_t version;
};

struct ZstdHeader {
    uint32_t version;
    uint32_t dictionary;
};

// Untested since zlib is deprecated and not worth spinning to test it.. for now.
inline ByteArray zlib_decompress(std::span<const uint8_t> span, uint32_t uncompressed_size)
{
    ByteArray result;
    std::size_t hdr_sz = sizeof(ZlibHeader);
    if (span.size() < hdr_sz) {
        LOG_F(ERROR, "Invalid Zlib Header");
        return result;
    }
    ZlibHeader hdr;
    memcpy(&hdr, span.data(), hdr_sz);
    switch (hdr.version) {
    default:
        LOG_F(ERROR, "Invalid Zlib version: {}", hdr.version);
        break;
    case 1: {
        result.resize(uncompressed_size);
        uLongf dst_len = 0;
        auto src = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(span.data()));
        auto err = uncompress(reinterpret_cast<Bytef*>(result.data()), &dst_len,
            src + hdr_sz, static_cast<uLongf>(span.size() - hdr_sz));

        if (err != Z_OK) {
            LOG_F(ERROR, "Zlib failed to decompress");
            result.clear();
        } else if (dst_len != uncompressed_size) {
            LOG_F(ERROR, "Zlib failed to decompress");
            result.clear();
        }
    } break;
    }
    return result;
}

inline ByteArray zstd_decompress(std::span<const uint8_t> span, uint32_t uncompressed_size)
{
    // Probably never will be multithreaded, but..
    static thread_local std::unique_ptr<ZSTD_DCtx, void (*)(ZSTD_DCtx*)> dctx{ZSTD_createDCtx(), detail::zstd_dctx_deleter};

    ByteArray result;
    std::size_t hdr_sz = sizeof(ZstdHeader);
    if (span.size() < hdr_sz) {
        LOG_F(ERROR, "Invalid Zstd Header");
        return result;
    }
    ZstdHeader hdr;
    memcpy(&hdr, span.data(), hdr_sz);
    switch (hdr.version) {
    default:
        LOG_F(ERROR, "Invalid Zstd version: {}", hdr.version);
        break;
    case 1: {
        result.resize(uncompressed_size);

        auto res_size = ZSTD_decompressDCtx(dctx.get(), result.data(), uncompressed_size,
            span.data() + hdr_sz, span.size() - hdr_sz);
        if (res_size != uncompressed_size) {
            LOG_F(ERROR, "zstd failed to decompress");
            result.clear();
        }
    } break;
    }
    return result;
}

ByteArray decompress(std::span<const uint8_t> span, const char* magic)
{
    auto hdr_sz = sizeof(CompressionHeader);
    ByteArray result;

    if (span.size() < sizeof(CompressionHeader)) {
        LOG_F(ERROR, "Compression header invalid");
        return result;
    } else if (strlen(magic) != 4) {
        LOG_F(ERROR, "Invalid magic byte sequence, must be 4 characters");
        return result;
    }

    CompressionHeader hdr;
    memcpy(&hdr, span.data(), hdr_sz);
    span = std::span<const uint8_t>{span.data() + hdr_sz, span.size() - hdr_sz};

    if (memcmp(hdr.magic.data(), magic, 4)) {
        LOG_F(ERROR, "mismatched magic bytes");
        return result;
    }

    switch (hdr.algorithm) {
    default:
        LOG_F(ERROR, "Invalid compression algorithm: {}", hdr.algorithm);
        break;
    case 0: // Uncompressed
        result = {span.data(), span.size()};
        break;
    case 1: // Zlib
        result = zlib_decompress(span, hdr.uncompressed_size);
        break;
    case 2: // Zstd
        result = zstd_decompress(span, hdr.uncompressed_size);
        break;
    }

    return result;
}

} // namespace nw
