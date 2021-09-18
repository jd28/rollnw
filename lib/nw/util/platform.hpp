#pragma once

#include <filesystem>

namespace nw {

struct NWNPaths {
    std::filesystem::path install;
    std::filesystem::path user;
};

NWNPaths get_nwn_paths();

} // namespace nw
