#pragma once

#include "Container.hpp"

#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

namespace nw {

using LocatorVariant = std::variant<Container*, std::unique_ptr<Container>>;

struct ResourceLocator {
    /// Add container by path
    bool add_container(const std::filesystem::path& path);

    /// Add already created container
    bool add_container(Container* container, bool take_ownership = true);

    /// Check if resource is in Locator
    bool contains(const Resource& res) const;

    /// Reads resourece data, empty ByteArray if no match.
    ByteArray demand(Resource res) const;

    /// Extract elements from a container by glob pattern
    int extract_by_glob(std::string_view glob, const std::filesystem::path& output) const;

    /// Extract elements from a container by regex
    int extract(const std::regex& pattern, const std::filesystem::path& output) const;

    /// Determines the size, if applicable, of the container
    size_t size() const;

    /// Get some general data about a resource
    ResourceDescriptor stat(const Resource& res) const;

    std::vector<LocatorVariant> containers_;
};

} // namespace nw
