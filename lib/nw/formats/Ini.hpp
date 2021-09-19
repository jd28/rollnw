#pragma once

#include "../util/ByteArray.hpp"
#include "../util/string.hpp"

#include <absl/container/flat_hash_map.h>
#include <inih/ini.h>

#include <filesystem>
#include <optional>
#include <string>

namespace nw {

class Ini {
public:
    Ini(const std::filesystem::path& filename);
    Ini(ByteArray bytes);

    template <typename T>
    std::optional<T> get(const std::string& key) const;
    bool get_to(const std::string& key, std::string& out) const;
    bool get_to(const std::string& key, int& out) const;
    bool get_to(const std::string& key, float& out) const;
    bool is_valid();

private:
    ByteArray bytes_;
    absl::flat_hash_map<std::string, std::string> map_;
    bool loaded_ = false;

    static int parse_ini(void* user, const char* section, const char* name, const char* value);
    bool parse();
};

template <typename T>
std::optional<T> Ini::get(const std::string& key) const
{
    std::string val;
    if (!get_to(key, val))
        return {};

    if constexpr (std::is_same_v<T, float> || std::is_convertible_v<T, int>) {
        return string::from<T>(val);
    } else {
        return val;
    }
}

} // namespace nw
