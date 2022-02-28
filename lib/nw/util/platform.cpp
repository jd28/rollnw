#include "platform.hpp"

#include <cstdlib>
#include <nowide/cstdlib.hpp>

#include <random>
#include <sstream>

namespace nw {

NWNPaths get_nwn_paths()
{
    // [TODO] PUNT, for now.
    NWNPaths result;

    if (auto p = nowide::getenv("NWN_HOME")) {
        result.user = p;
    }

    if (auto p = nowide::getenv("NWN_ROOT")) {
        result.install = p;
    }

    return result;
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
