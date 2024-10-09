#pragma once

#include "Container.hpp"

#include <unzip.h>

#include <filesystem>

namespace nw {

struct ZipElement {
    Resource resref;
    size_t size;
};

struct Zip : public Container {
    Zip(const std::filesystem::path& path);
    ~Zip();

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

private:
    String path_;
    String name_;
    unzFile file_ = nullptr;
    bool is_loaded_ = false;
    Vector<ZipElement> elements_;

    bool load();
};

} // namespace nw
