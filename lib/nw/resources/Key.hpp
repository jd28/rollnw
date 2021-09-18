/// @file
#pragma once

#include "Bif.hpp"
#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace nw {

/// @private
struct KeyTableElement {
    uint32_t bif;
    uint32_t index;
};

/**
 * @brief Key Table Format
 *
 */
struct Key : public Container {
    /// @name Constructors / Destructors
    /// @{
    explicit Key(std::filesystem::path path);
    Key(const Key&) = delete;
    Key(Key&&) = default;
    virtual ~Key() = default;
    /// @}

    virtual ByteArray demand(Resource res) override;

    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) override;

    /**
     * @brief Number of resources in the Key & Bif files.
     */
    virtual size_t size() const override;

    /// @name Operators
    /// @{
    Key& operator=(const Key&) = delete;
    Key& operator=(Key&&) = default;
    /// @}

private:
    std::filesystem::path path_;
    std::streamsize fsize_;
    std::vector<Bif> bifs_;
    absl::flat_hash_map<Resource, KeyTableElement> elements_;
    bool is_loaded_ = false;
    bool load();
};

} // namespace nw
