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

struct Key : public Container {
    explicit Key(std::filesystem::path path);
    Key(const Key&) = delete;
    Key(Key&&) = default;
    virtual ~Key() = default;

    /// Returns if Key file was successfully loaded
    bool is_loaded() const noexcept { return is_loaded_; }

    virtual std::vector<ResourceDescriptor> all() override;
    virtual ByteArray demand(Resource res) override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) override;
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) override;

    Key& operator=(const Key&) = delete;
    Key& operator=(Key&&) = default;

private:
    std::filesystem::path path_;
    std::streamsize fsize_;
    std::vector<Bif> bifs_;
    absl::flat_hash_map<Resource, KeyTableElement> elements_;
    bool is_loaded_ = false;
    bool load();
};

} // namespace nw
