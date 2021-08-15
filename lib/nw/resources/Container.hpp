#pragma once

#include "../util/ByteArray.hpp"
#include "Resource.hpp"

#include <filesystem>

namespace nw {

/**
 * @brief Base class for all containers.
 *
 */
struct Container {
    virtual ~Container() = default;

    /**
     * @brief Read resourece data
     *
     * @param res Qualified resource name
     * @return ByteArray If the container does not hold the resource, the resulting ``nw::ByteArray`` will be of size 0.
     */
    virtual ByteArray demand(Resource res) = 0;

    /**
     * @brief Extract elements from a container by glob pattern
     *
     */
    virtual int extract(std::string_view glob, const std::filesystem::path& output);

    /**
     * @brief Extract elements from a container by regex
     */
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) = 0;

    /**
     * @brief Determines the size, if applicable, of the container
     *
     * @return size_t
     */
    virtual size_t size() const { return 0; }
};

} // namespace nw
