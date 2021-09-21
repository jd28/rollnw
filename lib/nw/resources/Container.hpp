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
    virtual std::vector<Resource> all() = 0;

    /**
     * @brief Read resourece data
     *
     * @param res Qualified resource name
     * @return ByteArray If the container does not hold the resource, the resulting ``nw::ByteArray`` will be of size 0.
     */
    virtual ByteArray demand(Resource res) = 0;

    /// Extract elements from a container by glob pattern
    virtual int extract(std::string_view glob, const std::filesystem::path& output);

    /// Extract elements from a container by regex
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) = 0;

    /// Determines the size, if applicable, of the container
    virtual size_t size() const { return 0; }

    /// Get some general data about a resource
    virtual ResourceDescriptor stat(const Resource& res) = 0;
};

} // namespace nw
