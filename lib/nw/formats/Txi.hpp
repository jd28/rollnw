#pragma once

#include "../resources/assets.hpp"
#include "../util/string.hpp"

#include <absl/container/flat_hash_map.h>

#include <optional>

namespace nw {

struct Txi {
    Txi() = default;
    explicit Txi(ResourceData data);

    template <typename T>
    std::optional<T> get(String key) const;

    bool get_to(String key, String& out) const;

    template <typename T>
    bool get_to(String key, T& out) const;

    bool valid() const noexcept;

private:
    ResourceData data_;
    absl::flat_hash_map<String, String> map_;
    bool loaded_ = false;

    bool parse();
};

template <typename T>
std::optional<T> Txi::get(String key) const
{
    String val;
    if (!get_to(std::move(key), val)) {
        return {};
    }

    if constexpr (std::is_arithmetic_v<T>) {
        return string::from<T>(val);
    } else {
        return val;
    }
}

template <typename T>
bool Txi::get_to(String key, T& out) const
{
    static_assert(std::is_arithmetic_v<T>, "[txi] only numeric types are allowed");
    string::tolower(&key);
    auto it = map_.find(key);
    if (it == std::end(map_)) {
        return false;
    }

    auto opt = string::from<T>(it->second);
    if (!opt) {
        return false;
    }

    out = *opt;
    return true;
}

} // namespace nw
