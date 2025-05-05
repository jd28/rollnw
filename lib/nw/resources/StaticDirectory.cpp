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

ResourceData StaticDirectory::demand(const ContainerKey* key) const
{
    auto k = static_cast<const detail::StaticDirectoryKey*>(key);

    ResourceData data;
    data.name = k->name;
    data.bytes = ByteArray::from_file(k->path);
    return data;
}

size_t StaticDirectory::size() const
{
    return elements_.size();
}

void StaticDirectory::visit(std::function<void(Resource, const ContainerKey*)> visitor) const
{
    for (const auto& it : elements_) {
        visitor(it.name, reinterpret_cast<const ContainerKey*>(&it));
    }
}

void StaticDirectory::walk_directory(const std::filesystem::path& path)
{
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            auto res = Resource::from_path(entry.path());
            if (!res.valid()) { continue; }
            elements_.emplace_back(res, path_to_string(fs::canonical(entry.path())));
        }
    }

    std::sort(std::begin(elements_), std::end(elements_), [](const auto& lhs, const auto& rhs) {
        return lhs.path < rhs.path;
    });
}

} // namespace nw
