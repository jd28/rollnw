#include "base64.hpp"

#include <cstring>

namespace nw {

// From NWNX:EE, original author probably Sherincall or someone on stack overflow.
static const char base64_key[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

ByteArray from_base64(const std::string& in)
{
    ByteArray out;
    out.reserve(3 * in.length() / 4 + 4);

    static int Table[256];
    if (Table[43] == 0) {
        memset(Table, 0xFF, sizeof(Table));
        for (int i = 0; i < 64; i++)
            Table[static_cast<uint8_t>(base64_key[i])] = i;
    }

    int val = 0, valb = -8;
    for (char c : in) {
        if (Table[static_cast<uint8_t>(c)] == -1)
            break;
        val = (val << 6) + Table[static_cast<uint8_t>(c)];
        valb += 6;
        if (valb >= 0) {
            out.push_back(uint8_t((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string to_base64(std::span<const uint8_t> bytes)
{
    std::string out;
    out.reserve(4 * bytes.size() / 3 + 4);

    int val = 0, valb = -6;
    for (uint8_t c : bytes) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_key[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        out.push_back(base64_key[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (out.size() % 4) {
        out.push_back('=');
    }

    return out;
}

} // namespace nw
