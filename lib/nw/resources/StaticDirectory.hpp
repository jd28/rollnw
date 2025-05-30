#pragma once

#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>

namespace nw {

namespace detail {

struct StaticDirectoryKey : public ContainerKey {
    StaticDirectoryKey(Resource name_, String path_)
        : name{name_}
        , path{std::move(path_)}
    {
    }

    Resource name;
    String path;
};

} // namespace detail

/// A static directory takes a view, optionally recursively, of a directory
/// structure when created.
struct StaticDirectory final : public Container {
    explicit StaticDirectory(const std::filesystem::path& path,
        nw::MemoryResource* allocator = nw::kernel::global_allocator());
    virtual ~StaticDirectory() = default;

    /// Reads resource data, empty ResourceData if no match.
    ResourceData demand(const ContainerKey* key) const override;

    /// Gets canonical path for a resource
    String get_canonical_path(nw::Resource res) const;

    /// Gets the shortend name of the container, should be analog to `basename container`
    const String& name() const override { return name_; }

    /// Canonical path for the container
    const String& path() const override { return path_string_; }

    /// Gets the number of assets in the container
    size_t size() const override;

    /// Checks if container was successfully loaded
    bool valid() const noexcept override { return is_valid_; }

    /// Enumerates all assets in a container
    void visit(std::function<void(Resource, const ContainerKey*)> visitor) const override;

private:
    void walk_directory(const std::filesystem::path& path);

    std::vector<detail::StaticDirectoryKey> elements_;
    std::filesystem::path path_;
    String path_string_;
    String name_;
    bool is_valid_ = false;
};

} // namespace nw
