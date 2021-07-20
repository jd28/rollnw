#include "ByteArray.hpp"

#include "../log.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace nw {

ByteArray::ByteArray(const std::byte* buffer, size_t len)
{
    append(buffer, len);
}

void ByteArray::append(const void* buffer, size_t len)
{
    auto b = reinterpret_cast<const std::byte*>(buffer);
    reserve(size() + len);
    for (size_t i = 0; i < len; ++i) {
        push_back(b[i]);
    }
}

bool ByteArray::read_at(size_t offset, void* buffer, size_t sz)
{
    if (offset + sz >= size()) {
        return false;
    }
    memcpy(buffer, data() + offset, sz);
    return true;
}

std::string_view ByteArray::string_view() const
{
    return {reinterpret_cast<const char*>(data()), size()};
}

ByteArray ByteArray::from_file(const std::filesystem::path& path)
{
    ByteArray ba;

    if (fs::exists(path)) {
        std::ifstream f{path};
        auto size = fs::file_size(path);
        ba.resize(size);
        f.read((char*)ba.data(), size);
    } else {
        LOG_F(ERROR, "File '{}' does not exist", path.c_str());
    }

    return ba;
}

} // namespace nw
