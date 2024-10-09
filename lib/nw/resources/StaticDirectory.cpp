#include "StaticDirectory.hpp"

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

StaticDirectory::StaticDirectory(const fs::path& path)
{
    if (!fs::exists(path)) {
        LOG_F(WARNING, "'{}' does not exist", path);
        return;
    } else if (!fs::is_directory(path)) {
        LOG_F(WARNING, "'{}' is not a directory", path);
        return;
    }

    path_ = fs::canonical(path);
    path_string_ = path_to_string(path_);
    name_ = path_to_string(path_.stem());

    walk_directory(path_);

    LOG_F(INFO, "[resources] dir: loading - {}", path_string_);
    is_valid_ = true;
}

Vector<ResourceDescriptor> StaticDirectory::all() const
{
    Vector<ResourceDescriptor> result;
    for (const auto& [res, path] : resource_to_path_) {
        ResourceDescriptor rd{
            res,
            fs::file_size(path),
            static_cast<int64_t>(fs::last_write_time(path).time_since_epoch().count() / 1000),
            this,
        };

        result.push_back(rd);
    }

    return result;
}

bool StaticDirectory::contains(Resource res) const
{
    return resource_to_path_.find(res) != std::end(resource_to_path_);
}

ResourceData StaticDirectory::demand(Resource res) const
{
    ResourceData data;
    data.name = res;

    auto it = resource_to_path_.find(res);
    if (it == std::end(resource_to_path_)) {
        return data;
    }

    data.bytes = ByteArray::from_file(it->second);
    return data;
}

int StaticDirectory::extract(const std::regex& pattern, const fs::path& output) const
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

    String fname;
    for (const auto& [res, path] : resource_to_path_) {
        fname = res.filename();
        if (std::regex_match(fname, pattern)) {
            fs::copy_file(path, output);
        }
    }

    return count;
}

size_t StaticDirectory::size() const
{
    return resource_to_path_.size();
}

ResourceDescriptor StaticDirectory::stat(const Resource& res) const
{
    auto it = resource_to_path_.find(res);
    ResourceDescriptor result;
    if (it == std::end(resource_to_path_)) { return result; }
    if (fs::exists(it->second)) {
        result.name = res;
        result.mtime = int64_t(fs::last_write_time(it->second).time_since_epoch().count() / 1000);
        result.parent = this;
        result.size = fs::file_size(it->second);
    }
    return result;
}

void StaticDirectory::visit(std::function<void(const Resource&)> callback,
    std::initializer_list<ResourceType::type> types) const noexcept
{
    for (const auto& [res, _] : resource_to_path_) {
        if (types.size() && std::end(types) == std::find(std::begin(types), std::end(types), res.type)) {
            continue;
        }
        callback(res);
    }
}

String StaticDirectory::get_canonical_path(nw::Resource res) const
{
    auto it = resource_to_path_.find(res);
    if (it != std::end(resource_to_path_)) {
        return it->second;
    } else {
        return {};
    }
}

void StaticDirectory::walk_directory(const std::filesystem::path& path)
{
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            auto res = Resource::from_path(entry.path());
            if (!res.valid()) { continue; }

            auto it = resource_to_path_.find(res);
            if (it != std::end(resource_to_path_)) {
                LOG_F(WARNING, "[resources] duplicate resource: {} shadows {}", it->second,
                    path_to_string(entry.path()));
            } else {
                resource_to_path_.insert({res, path_to_string(fs::canonical(entry.path()))});
            }
        }
    }
}

} // namespace nw
