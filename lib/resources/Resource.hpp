#pragma once

#include "ResourceType.hpp"
#include "Resref.hpp"

#include <absl/hash/hash.h>

#include <cstddef>
#include <string>

namespace nw {

/**
 * @brief A ``nw::Resource`` consists of a ``nw::Resref`` and a ``nw::ResourceType``.
 * Since NWN1/EE doesn't have any notion of hierarchical originzation (paths, etc),
 * it represents a fully-qualified resource identifier.
 */
struct Resource {
    /// @name Constructors / Destructors
    /// @{
    Resource() noexcept;
    Resource(Resref resref, ResourceType::type type) noexcept;
    Resource(std::string_view resref, ResourceType::type type) noexcept;
    Resource(const Resource&) = default;
    Resource(Resource&&) = default;
    /// @}

    /// @name Members
    /// @{
    Resref resref;
    ResourceType::type type;
    /// @}

    /// @name Functions
    /// @{
    /**
     * @brief Gets a Resrefs file name with extension.
     */
    std::string filename() const;
    /// @}

    /// @name Operators
    /// @{
    Resource& operator=(const Resource&) = default;
    Resource& operator=(Resource&&) = default;

    /**
     * @return false if string is empty or resref type is invalid
     */
    bool valid() const noexcept;
    /// @}
};

template <typename H>
H AbslHashValue(H h, const Resource& res)
{
    return H::combine(std::move(h), res.resref.view(), res.type);
}

// [TODO] Replace with spaceship operator when c++20 is better supported.
bool operator==(const Resource& lhs, const Resource& rhs);
bool operator!=(const Resource& lhs, const Resource& rhs);
bool operator<(const Resource& lhs, const Resource& rhs) noexcept;

std::ostream& operator<<(std::ostream& out, const Resource& res);

} // namespace nw
