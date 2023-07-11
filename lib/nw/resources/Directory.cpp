#include "Directory.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/platform.hpp"
#include "../util/templates.hpp"

#include <nowide/convert.hpp>

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace nw {

Directory::Directory(const fs::path& path)
{
    if (!fs::exists(path)) {
        LOG_F(WARNING, "'{}' does not exist", path);
        return;
    } else if (!fs::is_directory(path)) {
        LOG_F(WARNING, "'{}' is not a directory", path);
        return;
    }

    path_ = fs::canonical(path);
    path_string_ = path_to_string(fs::canonical(path));
    name_ = path_to_string(path.parent_path().filename());

    LOG_F(INFO, "{}: Loading...", path_string_);
    is_valid_ = true;
}

std::vector<ResourceDescriptor> Directory::all() const
{
    std::vector<ResourceDescriptor> res;
    for (auto it : fs::directory_iterator{path_}) {
        if (!it.is_regular_file()) { continue; }

        auto fn = it.path().filename();

        ResourceType::type t = ResourceType::from_extension(fn.extension().string());
        if (t == ResourceType::invalid) { continue; }

        auto s = fn.stem().string();
        if (s.length() > 16) { continue; }

        ResourceDescriptor rd{
            Resource{s, t},
            fs::file_size(it.path()),
            static_cast<int64_t>(fs::last_write_time(it.path()).time_since_epoch().count() / 1000),
            this,
        };

        res.push_back(rd);
    }

    return res;
}

bool Directory::contains(Resource res) const
{
    fs::path p = path_ / res.filename();
    return fs::exists(p) && fs::is_regular_file(p);
}

ResourceData Directory::demand(Resource res) const
{
    fs::path p = path_ / res.filename();

    ResourceData data;
    data.name = res;

    if (!fs::exists(p) || !fs::is_regular_file(p)) {
        return data;
    }

    std::ifstream f{p, std::ios_base::binary};

    if (!f.is_open()) {
        LOG_F(INFO, "Skip - Unable to open file: {}", p);
        return data;
    }

    if (size_t size = fs::file_size(p)) {
        data.bytes.resize(size);
        if (!istream_read(f, data.bytes.data(), size)) {
            LOG_F(INFO, "Skip - Error reading file: {}", p);
            data.bytes.clear(); // Free the memory
        }
    }

    return data;
}

int Directory::extract(const std::regex& pattern, const fs::path& output) const
{
    if (!fs::is_directory(output)) {
        // Needs to be create_directories to simplify usage.  Alternatively,
        // could assert that path must already exist.  Needs error handling.
        fs::create_directories(output);
    } else if (path_ == fs::canonical(output)) {
        LOG_F(ERROR, "Attempting to extra files to the same directory");
        return 0;
    }

    int count = 0;

    for (auto it : fs::directory_iterator{path_}) {
        if (!it.is_regular_file()) { continue; }
        if (std::regex_match(it.path().stem().string(), pattern)) {
            ++count;
            fs::copy_file(it.path(), output);
        }
    }

    return count;
}

size_t Directory::size() const
{
    size_t result = 0;
    for (auto it : fs::directory_iterator{path_}) {
        if (!it.is_regular_file()) { continue; }

        auto fn = it.path().filename();

        ResourceType::type t = ResourceType::from_extension(fn.extension().string());
        if (t == ResourceType::invalid) { continue; }

        auto s = fn.stem().string();
        if (s.length() > 16) { continue; }
        ++result;
    }

    return result;
}

ResourceDescriptor Directory::stat(const Resource& res) const
{
    ResourceDescriptor result;
    auto p = path_ / res.filename();
    if (fs::exists(p)) {
        result.name = res;
        result.mtime = int64_t(fs::last_write_time(p).time_since_epoch().count() / 1000);
        result.parent = this;
        result.size = fs::file_size(p);
    }
    return result;
}

void Directory::visit(std::function<void(const Resource&)> callback) const noexcept
{
    for (auto it : fs::directory_iterator{path_}) {
        if (!it.is_regular_file()) { continue; }

        auto fn = it.path().filename();

        ResourceType::type t = ResourceType::from_extension(fn.extension().string());
        if (t == ResourceType::invalid) { continue; }

        auto s = fn.stem().string();
        if (s.length() > 16) { continue; }

        callback(Resource{s, t});
    }
}

} // namespace nw
