#pragma once

#include "../resources/ResourceData.hpp"
#include "../util/string.hpp"

#include <absl/container/flat_hash_map.h>
#include <inih/ini.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nw {

/**
 * @brief Ini file format parser
 *
 * Lookup is by "<section>/<key>"
 *
 * @note This is read only currently.
 */
struct Ini {
    Ini() = default;
    explicit Ini(const std::filesystem::path& filename);
    explicit Ini(ResourceData data);

    /**
     * @brief Gets a value
     *
     * @tparam T `int32_t`, `float`, or `String`
     * @param key
     * @return std::optional<T>
     */
    template <typename T>
    std::optional<T> get(String key) const;

    /// Gets string value
    bool get_to(String key, String& out) const;

    /// Gets numeric value
    template <typename T>
    bool get_to(String key, T& out) const;

    /// Deterimes if Ini file was successfully parsed
    bool valid() const noexcept;

private:
    ResourceData data_;
    absl::flat_hash_map<String, String> map_;
    bool loaded_ = false;

    static int parse_ini(void* user, const char* section, const char* name, const char* value);
    bool parse();
};

template <typename T>
std::optional<T> Ini::get(String key) const
{
    String val;
    if (!get_to(std::move(key), val))
        return {};

    if constexpr (std::is_arithmetic_v<T>) {
        return string::from<T>(val);
    } else {
        return val;
    }
}

template <typename T>
bool Ini::get_to(String key, T& out) const
{
    static_assert(std::is_arithmetic_v<T>, "[ini] only numeric types are allowed");
    string::tolower(&key);
    auto it = map_.find(key);
    if (it == std::end(map_))
        return false;

    auto opt = string::from<T>(it->second);
    if (!opt) {
        return false;
    }

    out = *opt;
    return true;
}

} // namespace nw
