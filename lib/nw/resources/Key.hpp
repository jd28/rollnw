/// @file
#pragma once

#include "Bif.hpp"
#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <memory>
#include <regex>
#include <string>

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

    virtual Vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const String& name() const override { return name_; };
    virtual const String& path() const override { return path_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return is_loaded_; }
    virtual void visit(std::function<void(const Resource&)> callback, std::initializer_list<ResourceType::type> types = {}) const noexcept override;

    Key& operator=(const Key&) = delete;
    Key& operator=(Key&&) = default;

private:
    String path_;
    String name_;
    std::streamsize fsize_;
    Vector<Bif> bifs_;
    absl::flat_hash_map<Resource, KeyTableElement> elements_;
    bool is_loaded_ = false;
    bool load();
};

} // namespace nw
