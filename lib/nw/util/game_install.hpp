#pragma once

#include <filesystem>

namespace nw {

enum struct InstallVersion {
    invalid,
    v1_69,
    vEE
};

struct InstallInfo {
    std::filesystem::path install;
    std::filesystem::path user;
    InstallVersion version = InstallVersion::invalid;
};

/// Probes for an NWN install
/// @param only probe for specific version
InstallInfo probe_nwn_install(InstallVersion only = InstallVersion::invalid);

} // namespace nw
