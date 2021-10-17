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
    Zip(std::filesystem::path filename);
    ~Zip();

    /// Get all resources
    virtual std::vector<Resource> all() override;

    /// Reads resourece data, empty ByteArray if no match.
    virtual ByteArray demand(Resource res) override;

    /// Extract elements from a container by regex
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) override;

    /// Determines the size, if applicable, of the container
    virtual size_t size() const override;

    /// Get some general data about a resource
    virtual ResourceDescriptor stat(const Resource& res) override;

    /// Get if zip was loaded
    bool valid() const noexcept { return is_loaded_; }

private:
    std::filesystem::path filename_;
    unzFile file_ = nullptr;
    size_t total_size = 0;
    bool is_loaded_ = false;
    std::vector<ZipElement> elements_;

    bool load();
};

} // namespace nw
