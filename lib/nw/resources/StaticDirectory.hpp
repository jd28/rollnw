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

    virtual std::vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return name_; };
    virtual const std::string& path() const override { return path_string_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return is_valid_; }
    virtual void visit(std::function<void(const Resource&)> callback, std::initializer_list<ResourceType::type> types = {}) const noexcept override;

    std::string get_canonical_path(nw::Resource res) const;
    void walk_directory(const std::filesystem::path& path);

private:
    absl::flat_hash_map<nw::Resource, std::string> resource_to_path_;
    std::filesystem::path path_;
    std::string path_string_;
    std::string name_;
    bool is_valid_ = false;
};

} // namespace nw
