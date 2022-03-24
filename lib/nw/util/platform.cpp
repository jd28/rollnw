#include "platform.hpp"

#include <nowide/convert.hpp>
#include <nowide/cstdlib.hpp>

#include <cstdlib>
#include <random>
#include <sstream>
#if defined(LIBNW_OS_LINUX) || defined(LIBNW_OS_MACOS)
#include <pwd.h>
#include <unistd.h>
#endif

namespace nw {

std::filesystem::path documents_path()
{
#if defined(LIBNW_OS_MACOS) || defined(LIBNW_OS_LINUX)
    return home_path() / "Documents"; // On macOS finder shows localized, but is "Documents"
                                      // on disk.  Don't know if this is true on Linux.
#elif defined(LIBNW_OS_WINDOWS)
    PWSTR path;
    if(SUCEEDED(SHGetKnownFolderPath(FOLDERID_Personal, KF_FLAG_DEFAULT, NULL, &path)) {
        return nowide::narrow(path);
    }
#endif
    throw std::runtime_error("unable to determine user document path");
}

std::filesystem::path home_path()
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
    return std::filesystem::path(home);
}

std::filesystem::path create_unique_tmp_path()
{
    static thread_local std::random_device dev;
    static thread_local std::mt19937 prng(dev());

    auto tmp_dir = std::filesystem::temp_directory_path();
    uint64_t i = 0;
    std::uniform_int_distribution<uint64_t> rand(0);
    std::filesystem::path path;
    while (true) {
        std::stringstream ss;
        ss << std::hex << rand(prng);
        path = tmp_dir / ss.str();
        if (std::filesystem::create_directory(path) || ++i > 100) {
            break;
        }
    }
    return path;
}

} // namespace nw
