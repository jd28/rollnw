#pragma once

#include "../util/ByteArray.hpp"
#include "../util/string.hpp"

#include <absl/container/flat_hash_map.h>
#include <inih/ini.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nw {

struct Ini {
    Ini() = default;
    explicit Ini(const std::filesystem::path& filename);
    explicit Ini(ByteArray bytes);

    template <typename T>
    std::optional<T> get(std::string key) const;
    bool get_to(std::string key, std::string& out) const;
    bool get_to(std::string key, int& out) const;
    bool get_to(std::string key, float& out) const;
    bool valid();

private:
    ByteArray bytes_;
    absl::flat_hash_map<std::string, std::string> map_;
    bool loaded_ = false;

    static int parse_ini(void* user, const char* section, const char* name, const char* value);
    bool parse();
};

template <typename T>
std::optional<T> Ini::get(std::string key) const
{
    std::string val;
    if (!get_to(std::move(key), val))
        return {};

    if constexpr (std::is_same_v<T, float> || std::is_convertible_v<T, int>) {
        return string::from<T>(val);
    } else {
        return val;
    }
}

} // namespace nw
