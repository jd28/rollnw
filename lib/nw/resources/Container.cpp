#include "Container.hpp"

#include "StaticDirectory.hpp"
#include "StaticErf.hpp"
#include "StaticKey.hpp"
#include "StaticZip.hpp"

#include "../util/platform.hpp"
#include "../util/string.hpp"

#include <array>
#include <optional>

namespace nw {

namespace fs = std::filesystem;

namespace {

std::optional<fs::path> find_existing_path(const fs::path& path)
{
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return path;
    }

    const fs::path parent = path.parent_path().empty() ? fs::path{"."} : path.parent_path();
    if (!fs::is_directory(parent, ec)) {
        return std::nullopt;
    }

    const auto filename = path.filename().string();
    fs::directory_iterator it{parent, ec};
    if (ec) {
        return std::nullopt;
    }

    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            return std::nullopt;
        }
        if (string::icmp(it->path().filename().string(), filename)) {
            return it->path();
        }
    }

    return std::nullopt;
}

Container* make_container(const fs::path& path, nw::MemoryResource* allocator)
{
    if (fs::is_directory(path)) {
        auto ptr = allocator->allocate(sizeof(StaticDirectory), alignof(StaticDirectory));
        return new (ptr) StaticDirectory{path, allocator};
    }

    const auto ext = path.extension().string();
    if (string::icmp(ext, ".hak") || string::icmp(ext, ".erf") || string::icmp(ext, ".mod")) {
        auto ptr = allocator->allocate(sizeof(StaticErf), alignof(StaticErf));
        return new (ptr) StaticErf{path, allocator};
    }
    if (string::icmp(ext, ".zip")) {
        auto ptr = allocator->allocate(sizeof(StaticZip), alignof(StaticZip));
        return new (ptr) StaticZip{path, allocator};
    }
    if (string::icmp(ext, ".key")) {
        auto ptr = allocator->allocate(sizeof(StaticKey), alignof(StaticKey));
        return new (ptr) StaticKey{path, allocator};
    }

    return nullptr;
}

bool is_container_extension(const fs::path& path)
{
    const auto ext = path_to_string(path.extension());
    return string::icmp(ext, ".hak")
        || string::icmp(ext, ".erf")
        || string::icmp(ext, ".mod")
        || string::icmp(ext, ".zip")
        || string::icmp(ext, ".key");
}

} // namespace

Container::Container(nw::MemoryResource* allocator)
    : allocator_(allocator)
{
}

nw::MemoryResource* Container::allocator() const noexcept
{
    return allocator_;
}

Container* resolve_container(const std::filesystem::path& p, const String& name,
    nw::MemoryResource* allocator)
{
    const fs::path label{name};
    std::array<fs::path, 6> candidates{
        p / label,
        p / (name + ".hak"),
        p / (name + ".erf"),
        p / (name + ".mod"),
        p / (name + ".zip"),
        p / (name + ".key"),
    };
    const size_t count = is_container_extension(label) ? 1 : candidates.size();

    for (size_t i = 0; i < count; ++i) {
        if (const auto path = find_existing_path(candidates[i])) {
            if (auto* result = make_container(*path, allocator)) {
                return result;
            }
        }
    }

    return nullptr;
}

} // namespace nw
