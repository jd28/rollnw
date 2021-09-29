#pragma once

#include "Container.hpp"

#include <filesystem>

namespace nw {

namespace fs = std::filesystem;

// Note that in the actual game, until the advent of the DEVELOPMENT folder, when a
// directory container was instantiated, the directory would be walked, and a static
// view of it would be created.  This only works, so far, as the DEVELOPMENT folder:
// it checks disk on every demand.

struct Directory : public Container {
    Directory(const std::filesystem::path& path);
    ~Directory() = default;

    virtual std::vector<Resource> all() override;
    virtual ByteArray demand(Resource res) override;
    virtual int extract(const std::regex& pattern, const std::filesystem::path& output) override;
    virtual ResourceDescriptor stat(const Resource& res) override;

private:
    std::filesystem::path path_;
};

} // namespace nw
