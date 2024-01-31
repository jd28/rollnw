#pragma once

#include <filesystem>

namespace nw {

enum struct GameVersion {
    invalid,
    v1_69,
    vEE,
    nwn2
};

struct InstallInfo {
    std::filesystem::path install;
    std::filesystem::path user;
    GameVersion version = GameVersion::invalid;
};

/// Probes for an NWN install
/// @param only probe for specific version
InstallInfo probe_nwn_install(GameVersion version);

} // namespace nw
