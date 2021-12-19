#include "ByteArray.hpp"

#include "../log.hpp"
#include "base64.hpp"

#include <nlohmann/json.hpp>
#include <nowide/fstream.hpp>

#include <cstdio>
#include <cstring>
#include <string_view>

namespace fs = std::filesystem;

namespace nw {

ByteArray::ByteArray(const uint8_t* buffer, size_t len)
{
    append(buffer, len);
}

void ByteArray::append(const void* buffer, size_t len)
{
    auto b = reinterpret_cast<const uint8_t*>(buffer);
    reserve(size() + len);
    for (size_t i = 0; i < len; ++i) {
        push_back(b[i]);
    }
}

bool ByteArray::read_at(size_t offset, void* buffer, size_t sz) const
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
        nowide::ifstream f{path.string(), std::ios_base::binary};
        auto size = fs::file_size(path);
        ba.resize(size);
        f.read((char*)ba.data(), size);
    } else {
        LOG_F(ERROR, "File '{}' does not exist", path.string());
    }

    return ba;
}

void from_json(const nlohmann::json& json, ByteArray& ba)
{
    if (json.is_string()) {
        ba = from_base64(json.get<std::string>());
    }
}

void to_json(nlohmann::json& json, const ByteArray& ba)
{
    json = to_base64(ba.span());
}

} // namespace nw
