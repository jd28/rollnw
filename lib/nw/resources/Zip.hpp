#pragma once

#include "Container.hpp"

#include <minizip/unzip.h>

#include <filesystem>
#include <vector>

namespace nw {

struct ZipElement {
    Resource resref;
    size_t size;
};

struct Zip : public Container {
    Zip(const std::filesystem::path& path);
    ~Zip();

    virtual std::vector<ResourceDescriptor> all() const override;
    virtual bool contains(Resource res) const override;
    virtual ResourceData demand(Resource res) const override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) const override;
    virtual const std::string& name() const override { return name_; };
    virtual const std::string& path() const override { return path_; };
    virtual size_t size() const override;
    virtual ResourceDescriptor stat(const Resource& res) const override;
    virtual bool valid() const noexcept override { return is_loaded_; }
    virtual void visit(std::function<void(const Resource&)> callback) const noexcept override;

private:
    std::string path_;
    std::string name_;
    unzFile file_ = nullptr;
    bool is_loaded_ = false;
    std::vector<ZipElement> elements_;

    bool load();
};

} // namespace nw
