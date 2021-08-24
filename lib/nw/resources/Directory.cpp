#include "Directory.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"

#include <cstring>
#include <fstream>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace nw {

Directory::Directory(const fs::path& path)
    : path_{fs::canonical(path)}
{
    LOG_F(INFO, "{}: Loading...", path_.c_str());
}

std::vector<Resource> Directory::all()
{
    std::vector<Resource> res;
    for (auto it : fs::directory_iterator{path_}) {
        if (!it.is_regular_file()) { continue; }

        auto fn = it.path().filename();

        ResourceType::type t = ResourceType::from_extension(fn.extension().c_str());
        if (t == ResourceType::invalid) { continue; }

        auto s = fn.stem().string();
        if (s.length() > 16) { continue; }

        res.emplace_back(s, t);
    }

    return res;
}

ByteArray Directory::demand(Resource res)
{
    fs::path p = path_ / res.filename();

    ByteArray ba;
    if (!fs::exists(p) || !fs::is_regular_file(p)) {
        return ba;
    }

    std::ifstream f{p};

    if (!f.is_open()) {
        LOG_F(INFO, "Skip - Unable to open file: {}", p.c_str());
        return ba;
    }

    if (size_t size = fs::file_size(p)) {
        ba.resize(size);
        if (!f.read((char*)ba.data(), size)) {
            LOG_F(INFO, "Skip - Error reading file: {}", p.c_str());
            ba.clear(); // Free the memory
        }
    }

    return ba;
}

int Directory::extract(const std::regex& pattern, const fs::path& output)
{
    if (!std::filesystem::is_directory(output)) {
        // Needs to be create_directories to simplify usage.  Alternatively,
        // could assert that path must already exist.  Needs error handling.
        std::filesystem::create_directories(output);
    } else if (path_ == fs::canonical(output)) {
        LOG_F(ERROR, "Attempting to extra files to the same directory");
        return 0;
    }

    int count = 0;

    for (auto it : fs::directory_iterator{path_}) {
        if (!it.is_regular_file()) { continue; }
        if (std::regex_match(it.path().stem().c_str(), pattern)) {
            ++count;
            fs::copy_file(it.path(), output);
        }
    }

    return count;
}

} // namespace nw
