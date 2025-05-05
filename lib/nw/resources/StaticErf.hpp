#pragma once

#include "../i18n/LocString.hpp"
#include "Container.hpp"
#include "ErfCommon.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>

namespace nw {

namespace detail {

/// @private
struct ErfKey : public ContainerKey {
    ErfKey(Resource name_, uint32_t offset_, uint32_t size_)
        : name{name_}
        , offset{offset_}
        , size{size_}
    {
    }

    Resource name;
    uint32_t offset;
    uint32_t size;
};

} // namespace detail

struct StaticErf final : public Container {
public:
    StaticErf() = default;
    explicit StaticErf(const std::filesystem::path& path);
    StaticErf(const StaticErf&) = delete;
    StaticErf(StaticErf&& other) = default;
    virtual ~StaticErf() = default;

    /// Reads resource data, empty ResourceData if no match.
    ResourceData demand(const ContainerKey* key) const override;

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

    /// Erf type.
    ErfType type = ErfType::erf;
    /// Version
    ErfVersion version = ErfVersion::v1_0;
    /// Description
    LocString description;

    StaticErf& operator=(const StaticErf&) = delete;
    StaticErf& operator=(StaticErf&&) = default;

private:
    String path_;
    String name_;
    std::streamsize fsize_;
    bool is_loaded_ = false;
    std::vector<detail::ErfKey> elements_;

    bool load(const std::filesystem::path& path);
    ResourceData read(const detail::ErfKey* element) const;
};

} // namespace nw
