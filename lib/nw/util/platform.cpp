#include "platform.hpp"
#include "../log.hpp"

#include <nowide/convert.hpp>
#include <nowide/cstdlib.hpp>

#include <cstdlib>
#include <random>
#include <sstream>
#if defined(LIBNW_OS_LINUX) || defined(LIBNW_OS_MACOS)
#include <pwd.h>
#include <unistd.h>
#elif defined(LIBNW_OS_WINDOWS)
#define WINVER 0x0A00
#define _WIN32_WINNT_WIN10 0x0A00 // Windows 10
#include <ShlObj.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace nw {

fs::path documents_path()
{
#if defined(LIBNW_OS_MACOS) || defined(LIBNW_OS_LINUX)
    return home_path() / "Documents"; // On macOS finder shows localized, but is "Documents"
                                      // on disk.  Don't know if this is true on Linux.
#elif defined(LIBNW_OS_WINDOWS)
    PWSTR path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &path))) {
        return nowide::narrow(path);
    }
#endif
    throw std::runtime_error("unable to determine user document path");
}

fs::path home_path()
{
    const char* home = nullptr;
#if defined(LIBNW_OS_MACOS) || defined(LIBNW_OS_LINUX)
    home = nowide::getenv("HOME");
    if (!home) {
        home = getpwuid(getuid())->pw_dir;
    }
#elif defined(LIBNW_OS_WINDOWS)
    home = nowide::getenv("USERPROFILE");
#endif
    if (!home) {
        throw std::runtime_error("unable to determine user home path");
    }
    return fs::path(home);
}

fs::path create_unique_tmp_path()
{
    static thread_local std::random_device dev;
    static thread_local std::mt19937 prng(dev());

    auto tmp_dir = fs::temp_directory_path();
    uint64_t i = 0;
    std::uniform_int_distribution<uint64_t> rand(0);
    fs::path path;
    while (true) {
        std::stringstream ss;
        ss << std::hex << rand(prng);
        path = tmp_dir / ss.str();
        if (fs::create_directory(path) || ++i > 100) {
            break;
        }
    }
    return path;
}

bool move_file_safely(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_F(ERROR, "Failed to write {}, error: {}", to, ec.value());
        return false;
    }
    fs::remove(from);
    return true;
}

} // namespace nw
