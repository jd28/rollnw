#pragma once

#include "Container.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>

namespace nw {

/// A static directory takes a view, optionally recursively, of a directory
/// structure when created.
struct StaticDirectory : public Container {
    StaticDirectory() = default;
    explicit StaticDirectory(const std::filesystem::path& path);
    virtual ~StaticDirectory() = default;

    virtual Vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const String& name() const override { return name_; };
    virtual const String& path() const override { return path_string_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return is_valid_; }
    virtual void visit(std::function<void(const Resource&)> callback, std::initializer_list<ResourceType::type> types = {}) const noexcept override;

    String get_canonical_path(nw::Resource res) const;
    void walk_directory(const std::filesystem::path& path);

private:
    absl::flat_hash_map<nw::Resource, String> resource_to_path_;
    std::filesystem::path path_;
    String path_string_;
    String name_;
    bool is_valid_ = false;
};

} // namespace nw
