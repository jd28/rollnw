#include "game_install.hpp"

#include "../log.hpp"
#include "platform.hpp"

#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>

#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace nw {

static Vector<std::filesystem::path> install_paths{
#if defined(ROLLNW_OS_LINUX)
    home_path() / ".local/share/Steam/steamapps/common/Neverwinter Nights/",
#elif defined(ROLLNW_OS_MACOS)
    home_path() / "Library/Application Support/Steam/steamapps/common/Neverwinter Nights/",
#elif defined(ROLLNW_OS_WINDOWS)
    "C:/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights/",
    "D:/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights/",
    "C:/Program Files (x86)/Neverwinter Nights/",
    "D:/Program Files (x86)/Neverwinter Nights/",
    "C:/Program Files (x86)/GOG Galaxy/Games/NWN Diamond/",
    "D:/Program Files (x86)/GOG Galaxy/Games/NWN Diamond/",
#endif
};

static Vector<std::filesystem::path> user_paths{
#if defined(ROLLNW_OS_MACOS) || defined(ROLLNW_OS_WINDOWS)
    documents_path() / "Neverwinter Nights"
#elif defined(ROLLNW_OS_LINUX)
    home_path() / ".local/share/Neverwinter Nights/",
#endif
};

InstallInfo probe_nwn_install(GameVersion version)
{
    InstallInfo result;
    bool install_found = false;

    // This probes both EE and 1.69 keys even though most of these paths are NWN:EE only.
    auto probe_keys = [&result, &install_found, version](const fs::path& p) {
        if (version == GameVersion::vEE && fs::exists(p / "data/nwn_base.key")) {
            result.install = p;
            install_found = true;
        } else if (version == GameVersion::v1_69 && fs::exists(p / "chitin.key")) {
            result.install = p;
            install_found = true;
        }
    };

    if (auto p = nowide::getenv("NWN_ROOT")) {
        if (p) {
            fs::path pp{p};
            if (!fs::exists(pp / "data/nwn_base.key") && !fs::exists(pp / "chitin.key")) {
                LOG_F(ERROR, "$NWN_ROOT is set to a path that does not contain the right game files: '{}'", p);
            } else {
                probe_keys(pp);
            }
        }
    }

    if (!install_found) {
        for (const auto& p : install_paths) {
            probe_keys(p);
            if (install_found) { break; }
        }
    }

    // [TODO] Windows Registery

    // In 1.69 user and install paths are the same
    if (result.version == GameVersion::v1_69) {
        result.user = result.install;
        return result;
    }

    if (auto p = nowide::getenv("NWN_HOME")) {
        fs::path pp{p};
        if (fs::exists(pp)) {
            result.user = pp;
        }
    }

    for (const auto& p : user_paths) {
        if (fs::exists(p)) {
            result.user = p;
        }
    }

    return result;
}

} // namespace nw
