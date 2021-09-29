#pragma once

#include <filesystem>

namespace nw {

struct NWNPaths {
    std::filesystem::path install;
    std::filesystem::path user;
};

/// Gets user and install paths.
/// WARNING - HUGE PUNT - JUST READS ENV VARS ``NWN_ROOT`` and ``NWN_HOME``
NWNPaths get_nwn_paths();

} // namespace nw
