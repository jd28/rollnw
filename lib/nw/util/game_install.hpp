#pragma once

#include <filesystem>

namespace nw {

enum struct GameVersion {
    invalid,
    v1_69,
    vEE,
    nwn2
};

/// Path aliases, some of these are EE only.
enum struct PathAlias {
    ambient,
    cache,
    currentgame,
    database,
    development,
    dmvault,
    hak,
    hd0,
    localvault,
    logs,
    modelcompiler,
    modules,
    movies,
    music,
    nwsync,
    oldservervault,
    override,
    patch,
    portraits,
    saves,
    screenshots,
    servervault,
    temp,
    tempclient,
    tlk,
    user = hd0,
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
