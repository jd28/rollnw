#include "Ini.hpp"

#include "../log.hpp"
#include "../util/string.hpp"

namespace fs = std::filesystem;

namespace nw {

Ini::Ini(const fs::path& filename)
    : bytes_{ByteArray::from_file(filename)}
{
    loaded_ = parse();
}

Ini::Ini(ByteArray bytes)
    : bytes_{std::move(bytes)}
{
    loaded_ = parse();
}

bool Ini::get_to(std::string key, std::string& out) const
{
    string::tolower(&key);
    auto it = map_.find(key);
    if (it == std::end(map_))
        return false;
    out = it->second;
    return true;
}

bool Ini::get_to(std::string key, int& out) const
{
    string::tolower(&key);
    auto it = map_.find(key);
    if (it == std::end(map_))
        return false;

    auto opt = string::from<int>(it->second);
    if (!opt) {
        return false;
    }

    out = *opt;
    return true;
}

bool Ini::get_to(std::string key, float& out) const
{
    string::tolower(&key);
    auto it = map_.find(key);
    if (it == std::end(map_))
        return false;

    auto opt = string::from<float>(it->second);
    if (!opt) {
        return false;
    }

    out = *opt;
    return true;
}

bool Ini::valid() const noexcept
{
    return loaded_;
}

// ---- Private ---------------------------------------------------------------

bool Ini::parse()
{
    if (bytes_.size() == 0) { return false; }
    int result = ini_parse_string(reinterpret_cast<const char*>(bytes_.data()), bytes_.size(), parse_ini, this);
    if (result) {
        LOG_F(ERROR, "Failed to parse, error on line: {}", result);
        return false;
    }
    return true;
}

int Ini::parse_ini(void* user, const char* section, const char* name, const char* value)
{
    if (!name) // Happens when INI_CALL_HANDLER_ON_NEW_SECTION enabled
        return 1;
    Ini* reader = static_cast<Ini*>(user);
    std::string key = std::string(section) + "/" + name;
    string::tolower(&key);

    reader->map_[key] = value ? value : "";
    return 1;
}

} // namespace nw
