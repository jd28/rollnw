#include "../log.hpp"
#include "platform.hpp"

#include <nowide/convert.hpp>
#include <nowide/cstdlib.hpp>

#include <cstdlib>
#include <random>
#include <sstream>
#if defined(ROLLNW_OS_LINUX) || defined(ROLLNW_OS_MACOS)
#include <pwd.h>
#include <unistd.h>
#elif defined(ROLLNW_OS_WINDOWS)
#define WINVER 0x0A00
#define _WIN32_WINNT_WIN10 0x0A00 // Windows 10
#include <ShlObj.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace nw {

fs::path documents_path()
{
#if defined(ROLLNW_OS_MACOS) || defined(ROLLNW_OS_LINUX)
    return home_path() / "Documents"; // On macOS finder shows localized, but is "Documents"
                                      // on disk.  Don't know if this is true on Linux.
#elif defined(ROLLNW_OS_WINDOWS)
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
#if defined(ROLLNW_OS_MACOS) || defined(ROLLNW_OS_LINUX)
    home = nowide::getenv("HOME");
    if (!home) {
        home = getpwuid(getuid())->pw_dir;
    }
#elif defined(ROLLNW_OS_WINDOWS)
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

std::filesystem::path expand_path(const std::filesystem::path& path)
{
    std::filesystem::path result;
    bool first = true;
    for (const auto& p : path) {
        if (p == "~" && first) {
            result /= home_path();
        } else if (p.c_str()[0] == '$') {
            auto envp = nowide::getenv(p.string().substr(1).c_str());
            result /= envp ? std::filesystem::path(envp) : p;
        }
#ifdef ROLLNW_OS_WINDOWS
        else if (p.c_str()[0] == '%') {
            auto string = p.string();
            auto envp = nowide::getenv(string.substr(1, string.size() - 1).c_str());
            result /= envp ? std::filesystem::path(envp) : p;
        }
#endif
        else {
            result /= p;
        }
        first = false;
    }
    return result;
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

std::string path_to_string(const std::filesystem::path& path)
{
#ifdef _MSC_VER
    return nowide::narrow(path.c_str());
#else
    return path.string();
#endif
}

} // namespace nw
