/// @file
#pragma once

#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <memory>
#include <regex>
#include <string>

namespace nw {

struct ByteArray;
struct StaticKey;

namespace detail {

struct BifElement {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t type;
};

/// Bif is used only by ``nw::Key``, it has no independant use.
struct Bif {
    Bif(StaticKey* key, std::filesystem::path path, nw::MemoryResource* allocator);

    // LCOV_EXCL_START
    Bif(const Bif&) = delete;
    Bif(Bif&& other) = default;

    Bif& operator=(const Bif&) = delete;
    Bif& operator=(Bif&& other) = default;
    // LCOV_EXCL_STOP

    ByteArray demand(size_t index) const;

    StaticKey* key_ = nullptr;
    std::filesystem::path path_;
    std::streamsize fsize_;
    PVector<BifElement> elements;
    bool is_loaded_;
    nw::MemoryResource* allocator_;

    bool load();
};

/// @private
struct KeyTableElement {
    uint32_t bif;
    uint32_t index;
};

/// @private
struct StaticKeyKey : public ContainerKey {
    StaticKeyKey(Resource name_, KeyTableElement element_)
        : name{name_}
        , element{element_}
    {
    }

    Resource name;
    KeyTableElement element;
};

} // namespace detail

struct StaticKey final : public Container {
    explicit StaticKey(std::filesystem::path path, nw::MemoryResource* allocator = nw::kernel::global_allocator());
    StaticKey(const StaticKey&) = delete;
    StaticKey(StaticKey&&) = default;
    virtual ~StaticKey() = default;

    /// Reads resource data, empty ResourceData if no match.
    ResourceData demand(const ContainerKey* key) const override;

    /// Extracts resources by regex
    /// @note If one prefers glob syntax see `nw::string::glob_to_regex`
    int extract(const std::regex& pattern, const std::filesystem::path& output) const;

    /// Gets the shortend name of the container, should be analog to `basename container`
    const String& name() const override { return name_; }

    /// Canonical path for the container
    const String& path() const override { return path_; }

    /// Gets the number of assets in the container
    size_t size() const override;

    /// Checks if container was successfully loaded
    bool valid() const noexcept override { return is_loaded_; }

    /// Enumerates all assets in a container
    void visit(std::function<void(Resource, const ContainerKey*)> visitor) const override;

    StaticKey& operator=(const StaticKey&) = delete;
    StaticKey& operator=(StaticKey&&) = default;

private:
    String path_;
    String name_;
    std::streamsize fsize_;
    PVector<detail::Bif> bifs_;
    PVector<detail::StaticKeyKey> elements_;
    bool is_loaded_ = false;

    bool load();
};

} // namespace nw
