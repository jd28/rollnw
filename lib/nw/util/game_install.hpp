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

/// Probes the process's NWN configuration for the requested game version.
/// Valid NWN_ROOT/NWN_HOME overrides take precedence over automatic discovery.
/// NWN_HOME names the game user directory itself, not its Documents parent.
/// Legacy 1.69 installations use their install directory for user data.
/// Missing/invalid candidates are skipped; version is invalid if no install is found.
/// This is a singleton query for process configuration, not an area/object transform.
InstallInfo probe_nwn_install(GameVersion version);

} // namespace nw
