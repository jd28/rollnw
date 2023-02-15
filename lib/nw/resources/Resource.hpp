#pragma once

#include "ResourceType.hpp"
#include "Resref.hpp"

#include <absl/hash/hash.h>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <tuple>

namespace nw {

/**
 * @brief A ``nw::Resource`` consists of a ``nw::Resref`` and a ``nw::ResourceType``.
 * Since NWN1/EE doesn't have any notion of hierarchical organization (paths, etc),
 * it represents a fully-qualified resource identifier.
 */
struct Resource {
    Resource() noexcept;
    Resource(const Resref& resref_, ResourceType::type type_) noexcept;
    Resource(std::string_view resref_, ResourceType::type type_) noexcept;
    Resource(const Resource&) = default;
    Resource(Resource&&) = default;

    static Resource from_filename(std::string_view);
    static Resource from_path(const std::filesystem::path& path);

    Resref resref;
    ResourceType::type type;

    /// Gets a Resrefs file name with extension.
    std::string filename() const;

    /// A resource is valid if resref is not empty and resref type is not invalid
    bool valid() const noexcept;

    Resource& operator=(const Resource&) = default;
    Resource& operator=(Resource&&) = default;
};

template <typename H>
H AbslHashValue(H h, const Resource& res)
{
    return H::combine(std::move(h), res.resref.view(), res.type);
}

inline bool operator==(const Resource& lhs, const Resource& rhs)
{
    return std::tie(lhs.resref, lhs.type) == std::tie(rhs.resref, rhs.type);
}

inline bool operator<(const Resource& lhs, const Resource& rhs)
{
    return std::tie(lhs.resref, lhs.type) < std::tie(rhs.resref, rhs.type);
}

std::ostream& operator<<(std::ostream& out, const Resource& res);

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, Resource& r);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, const Resource& r);

} // namespace nw
