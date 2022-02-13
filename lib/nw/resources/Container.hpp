#pragma once

#include "../util/ByteArray.hpp"
#include "Resource.hpp"
#include "ResourceDescriptor.hpp"

#include <filesystem>
#include <vector>

namespace nw {

/// Base class for all containers.
struct Container {
    virtual ~Container() = default;

    /// Get all resources
    virtual std::vector<ResourceDescriptor> all() = 0;

    /// Reads resourece data, empty ByteArray if no match.
    virtual ByteArray demand(Resource res) = 0;

    /// Extract elements from a container by glob pattern
    virtual int extract_by_glob(std::string_view glob, const std::filesystem::path& output);

    /// Extract elements from a container by regex
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) = 0;

    /// Determines the size, if applicable, of the container
    virtual size_t size() const { return 0; }

    /// Get some general data about a resource
    virtual ResourceDescriptor stat(const Resource& res) = 0;
};

} // namespace nw
