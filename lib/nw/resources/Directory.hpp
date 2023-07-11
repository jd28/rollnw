#pragma once

#include "Container.hpp"

#include <filesystem>

namespace nw {

// Note that in the actual game, until the advent of the DEVELOPMENT folder, when a
// directory container was instantiated, the directory would be walked, and a static
// view of it would be created.  This only works, so far, as the DEVELOPMENT folder:
// it checks disk on every demand.

struct Directory : public Container {
    Directory() = default;
    explicit Directory(const std::filesystem::path& path);
    virtual ~Directory() = default;

    virtual std::vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return name_; };
    virtual const std::string& path() const override { return path_string_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return is_valid_; }
    virtual void visit(std::function<void(const Resource&)> callback) const noexcept override;

private:
    std::filesystem::path path_;
    std::string path_string_;
    std::string name_;
    bool is_valid_ = false;
};

} // namespace nw
