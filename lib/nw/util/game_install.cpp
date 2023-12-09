#include "game_install.hpp"

#include "../log.hpp"
#include "platform.hpp"

#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>

#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace nw {

static std::vector<std::filesystem::path> install_paths
{
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

#if defined(ROLLNW_OS_LINUX)
static std::filesystem::path beamdog_settings = home_path() / ".config/Beamdog Client/settings.json";
#elif defined(ROLLNW_OS_MACOS)
static std::filesystem::path beamdog_settings = home_path() / "Library/Application Support/Beamdog Client/settings.json";
#elif defined(ROLLNW_OS_WINDOWS)
static std::filesystem::path beamdog_settings = home_path() / "Beamdog Client/settings.json";
#endif

static std::vector<std::filesystem::path> user_paths
{
#if defined(ROLLNW_OS_MACOS) || defined(ROLLNW_OS_WINDOWS)
    documents_path() / "Neverwinter Nights"
#elif defined(ROLLNW_OS_LINUX)
    home_path() / ".local/share/Neverwinter Nights/",
#endif
};

struct BeamdogInstall {
    const char* appid;
    const char* path;
};

static constexpr BeamdogInstall beamdog_versions[] = {
    {"00840", "NWN EE Digital Deluxe Beta (Head Start)"},
    {"00829", "NWN EE Beta (Head Start)"},
    {"00839", "NWN EE Digital Deluxe"},
    {"00785", "NWN EE"}};

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
        fs::path pp{p};
        probe_keys(pp);
    }

    if (!install_found) {
        for (const auto& p : install_paths) {
            probe_keys(p);
            if (install_found) { break; }
        }
    }

    if (!install_found) {
        if (fs::exists(beamdog_settings)) {
            std::filesystem::path bd_root;
            try {
                std::ifstream i{beamdog_settings};
                nlohmann::json j;
                i >> j;
                const auto& folders = j.at("folders");
                folders[0].get_to(bd_root);
                for (const auto bdv : beamdog_versions) {
                    if (fs::exists(bd_root / bdv.appid)) {
                        probe_keys(bd_root / bdv.appid);
                        if (install_found) { break; }
                    }
                }
            } catch (const std::exception& e) {
                LOG_F(ERROR, "Failed parsing beamdog client settings ({}) - {}", beamdog_settings, e.what());
            }
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
