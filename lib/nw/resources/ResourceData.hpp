#pragma once

#include "../util/ByteArray.hpp"
#include "Resource.hpp"

#include <filesystem>

namespace nw {

struct ResourceData {
    ResourceData() = default;
    ResourceData(const ResourceData&) = delete;
    ResourceData(ResourceData&&) = default;
    ResourceData& operator=(const ResourceData&) = delete;
    ResourceData& operator=(ResourceData&&) = default;

    Resource name;
    ByteArray bytes;

    bool operator==(const ResourceData& other) const = default;
    ResourceData copy() const;

    static ResourceData from_file(const std::filesystem::path& path);
};

} // namespace nw
