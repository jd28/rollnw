#pragma once

#include "../util/ByteArray.hpp"
#include "Resource.hpp"
#include "ResourceData.hpp"
#include "ResourceDescriptor.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace nw {

/// Base class for all containers.
struct Container {
    Container();
    virtual ~Container();

    /// Get all resources
    virtual std::vector<ResourceDescriptor> all() const = 0;

    /// Get if container contains resource
    virtual bool contains(Resource res) const = 0;

    /// Reads resource data, empty ResourceData if no match.
    virtual ResourceData demand(Resource res) const = 0;

    /// Extract elements from a container by glob pattern
    virtual int extract_by_glob(std::string_view glob, const std::filesystem::path& output) const;

    /// Extract elements from a container by regex
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const = 0;

    /// Equivalent to `basename path()`
    virtual const std::string& name() const = 0;

    /// Path to container, for basic containers, should be canonical
    virtual const std::string& path() const = 0;

    /// Determines the size, if applicable, of the container
    virtual size_t size() const { return 0; }

    /// Get some general data about a resource
    virtual ResourceDescriptor stat(const Resource& res) const = 0;

    /// Return true if loaded, false if not.
    virtual bool valid() const noexcept = 0;

    /// Visits all resources in a container
    virtual void visit(std::function<void(const Resource&)> callback) const noexcept = 0;

    /// Get container working directory
    const std::filesystem::path& working_directory() const;

private:
    std::filesystem::path working_dir_;
};

using unique_container = std::unique_ptr<Container>;

} // namespace nw
