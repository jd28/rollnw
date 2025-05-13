#pragma once

#include "Container.hpp"
#include "assets.hpp"

#include <zip.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace nw {

namespace detail {
struct StaticZipKey : public ContainerKey {
    StaticZipKey(const Resource& name_, const String& path_, size_t size_, zip_uint64_t index_)
        : name(name_)
        , path(path_)
        , size(size_)
        , index(index_)
    {
    }

    Resource name;
    String path;
    size_t size;
    zip_uint64_t index;
};
} // namespace detail

class StaticZip : public Container {
public:
    explicit StaticZip(const std::filesystem::path& path);
    ~StaticZip() override;

    /// Reads resource data, empty ResourceData if no match.
    ResourceData demand(const ContainerKey* key) const override;

    /// Gets the shortend name of the container, should be analog to `basename container`
    const String& name() const override;

    /// Canonical path for the container
    const String& path() const override;

    /// Gets the number of assets in the container
    size_t size() const override;

    /// Checks if container was successfully loaded
    bool valid() const noexcept override;

    /// Enumerates all assets in a container
    void visit(std::function<void(Resource, const ContainerKey*)> visitor) const override;

private:
    bool load();

    String name_;
    String path_;
    bool is_loaded_ = false;

    std::vector<detail::StaticZipKey> elements_;
};

} // namespace nw
