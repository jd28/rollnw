#pragma once

#include "../config.hpp"
#include "../kernel/Memory.hpp"
#include "assets.hpp"

#include <filesystem>
#include <memory>

namespace nw {

/// Container key is opaque type, used below in ``demand`` and ``enumerate`` below. It ought to
/// be a pointer to something that the container can use for O(1) lookup.
struct ContainerKey {
    virtual ~ContainerKey() = default;
};

/// Base class for all containers.
struct Container {
    Container(nw::MemoryResource* allocator);
    virtual ~Container() = default;

    /// Gets allocator
    nw::MemoryResource* allocator() const noexcept;

    /// Reads resource data, empty ResourceData if no match.
    virtual ResourceData demand(const ContainerKey* key) const = 0;

    /// Equivalent to `basename path()`
    virtual const String& name() const = 0;

    /// Path to container, for basic containers, should be canonical
    virtual const String& path() const = 0;

    /// Determines the size, if applicable, of the container
    virtual size_t size() const = 0;

    /// Return true if loaded, false if not.
    virtual bool valid() const noexcept = 0;

    /// Enumerates all assets in a container
    virtual void visit(std::function<void(Resource, const ContainerKey*)> visitor) const = 0;

private:
    nw::MemoryResource* allocator_;
};

struct ContainerDeleter {
    ContainerDeleter(nw::MemoryResource* allocator_)
        : allocator{allocator_}
    {
    }

    nw::MemoryResource* allocator = nullptr;

    void operator()(Container* c) const
    {
        if (c && allocator) {
            c->~Container();
            allocator->deallocate(c);
        }
    }
};

using unique_container = std::unique_ptr<Container, ContainerDeleter>;

/// Loads a container from the specified path and name by folder or by file type.
/// @note The caller will be the owner of the returned container.
Container* resolve_container(const std::filesystem::path& p, const String& name,
    nw::MemoryResource* allocator);

} // namespace nw
