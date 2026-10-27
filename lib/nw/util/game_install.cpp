#include "game_install.hpp"

#include "../log.hpp"
#include "platform.hpp"

#include <nowide/cstdlib.hpp>

#include <array>
#include <span>
#include <stdexcept>

#if defined(ROLLNW_OS_WINDOWS)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace nw {
namespace {

fs::path probe_install_paths(GameVersion version, std::span<const fs::path> paths)
{
    const auto key = version == GameVersion::vEE ? "data/nwn_base.key" : "chitin.key";
    for (const auto& path : paths) {
        std::error_code error;
        if (!path.empty() && fs::is_regular_file(path / key, error)) {
            return path;
        }
    }
    return {};
}

#if defined(ROLLNW_OS_WINDOWS)
Vector<fs::path> gog_ee_install_paths()
{
    // GOG records the actual directory here, including custom drives/folders.
    constexpr auto key = L"SOFTWARE\\GOG.com\\Games\\1097893768";
    Vector<fs::path> paths;
    for (const DWORD view : {RRF_SUBKEY_WOW6432KEY, RRF_SUBKEY_WOW6464KEY}) {
        const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | view;
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"path", flags,
                nullptr, nullptr, &bytes)
                != ERROR_SUCCESS
            || bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0
            || bytes > 32'768 * sizeof(wchar_t)) {
            continue;
        }
        Vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
        bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"path", flags,
                nullptr, value.data(), &bytes)
                == ERROR_SUCCESS
            && value.front() != L'\0') {
            paths.emplace_back(value.data());
        }
    }
    return paths;
}
#endif

} // namespace

InstallInfo probe_nwn_install(GameVersion version)
{
    InstallInfo result;
    if (version != GameVersion::vEE && version != GameVersion::v1_69) {
        return result;
    }

    if (const auto* root = nowide::getenv("NWN_ROOT"); root && root[0] != '\0') {
        result.install = probe_install_paths(version, std::array{path_from_string(root)});
        if (result.install.empty()) {
            LOG_F(ERROR, "$NWN_ROOT does not contain the requested game's key file: '{}'", root);
        }
    }

#if defined(ROLLNW_OS_WINDOWS)
    if (result.install.empty() && version == GameVersion::vEE) {
        result.install = probe_install_paths(version, gog_ee_install_paths());
    }
#endif

    if (result.install.empty()) {
        const fs::path install_paths[]{
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
        result.install = probe_install_paths(version, install_paths);
    }

    if (!result.install.empty()) { result.version = version; }
    // In 1.69 user and install paths are the same
    if (result.version == GameVersion::v1_69) {
        result.user = result.install;
        return result;
    }

    if (const auto* user = nowide::getenv("NWN_HOME"); user && user[0] != '\0') {
        auto path = path_from_string(user);
        std::error_code error;
        if (fs::is_directory(path, error)) {
            result.user = std::move(path);
            return result;
        }
        LOG_F(ERROR, "$NWN_HOME is not an accessible directory: '{}'", user);
    }

    // Resolve the current known folder only if the explicit override did not
    // supply a directory. Windows may redirect Documents into OneDrive.
    try {
#if defined(ROLLNW_OS_MACOS) || defined(ROLLNW_OS_WINDOWS)
        const auto path = documents_path() / "Neverwinter Nights";
#elif defined(ROLLNW_OS_LINUX)
        const auto path = home_path() / ".local/share/Neverwinter Nights";
#endif
        std::error_code error;
        if (fs::is_directory(path, error)) {
            result.user = path;
        }
    } catch (const std::runtime_error& error) {
        LOG_F(ERROR, "Unable to resolve the NWN user directory: {}", error.what());
    }

    return result;
}

} // namespace nw
