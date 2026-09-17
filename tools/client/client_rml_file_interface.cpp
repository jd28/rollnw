#include "client_rml_file_interface.hpp"

#include <nw/resources/ResourceManager.hpp>
#include <nw/util/ByteArray.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <utility>

namespace nw::toolset {
namespace {

struct RmlResourceFile {
    nw::ByteArray bytes;
    size_t position = 0;
    std::FILE* fallback = nullptr;
};

} // namespace

ClientRmlFileInterface::ClientRmlFileInterface(
    nw::ResourceManager& ui_resources, nw::ResourceManager& game_resources)
    : ui_resources_(&ui_resources)
    , game_resources_(&game_resources)
{
}

Rml::FileHandle ClientRmlFileInterface::Open(const Rml::String& path)
{
    if (auto data = demand(path); data.bytes.size()) {
        auto* file = new RmlResourceFile{};
        file->bytes = std::move(data.bytes);
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    Rml::String fallback_path = path;
    constexpr std::string_view file_protocol = "file://";
    if (fallback_path.rfind(file_protocol, 0) == 0) {
        fallback_path.erase(0, file_protocol.size());
#if defined(_WIN32)
        if (fallback_path.size() >= 3 && fallback_path[0] == '/'
            && fallback_path[2] == ':') {
            fallback_path.erase(0, 1);
        }
#endif
    }
    std::replace(fallback_path.begin(), fallback_path.end(), '|', ':');
    if (auto* fallback = std::fopen(fallback_path.c_str(), "rb")) {
        auto* file = new RmlResourceFile{};
        file->fallback = fallback;
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    return {};
}

void ClientRmlFileInterface::Close(Rml::FileHandle handle)
{
    auto* file = reinterpret_cast<RmlResourceFile*>(handle);
    if (!file) {
        return;
    }
    if (file->fallback) {
        std::fclose(file->fallback);
    }
    delete file;
}

size_t ClientRmlFileInterface::Read(void* buffer, size_t size, Rml::FileHandle handle)
{
    auto* file = reinterpret_cast<RmlResourceFile*>(handle);
    if (!file || !buffer || size == 0) {
        return 0;
    }
    if (file->fallback) {
        return std::fread(buffer, 1, size, file->fallback);
    }

    const size_t available = file->position < file->bytes.size() ? file->bytes.size() - file->position : 0;
    const size_t to_read = std::min(size, available);
    if (to_read > 0) {
        std::memcpy(buffer, file->bytes.data() + file->position, to_read);
        file->position += to_read;
    }
    return to_read;
}

bool ClientRmlFileInterface::Seek(Rml::FileHandle handle, long offset, int origin)
{
    auto* file = reinterpret_cast<RmlResourceFile*>(handle);
    if (!file) {
        return false;
    }
    if (file->fallback) {
        return std::fseek(file->fallback, offset, origin) == 0;
    }

    size_t base = 0;
    if (origin == SEEK_SET) {
        base = 0;
    } else if (origin == SEEK_CUR) {
        base = file->position;
    } else if (origin == SEEK_END) {
        base = file->bytes.size();
    } else {
        return false;
    }

    if (offset >= 0) {
        if (std::cmp_greater(offset, file->bytes.size() - base)) {
            return false;
        }
        file->position = base + static_cast<size_t>(offset);
    } else {
        // Negate offset + 1 so LONG_MIN is representable, then add in unsigned.
        const auto magnitude = static_cast<unsigned long>(-(offset + 1)) + 1;
        if (std::cmp_greater(magnitude, base)) {
            return false;
        }
        file->position = base - static_cast<size_t>(magnitude);
    }
    return true;
}

size_t ClientRmlFileInterface::Tell(Rml::FileHandle handle)
{
    auto* file = reinterpret_cast<RmlResourceFile*>(handle);
    if (!file) {
        return 0;
    }
    if (file->fallback) {
        const long position = std::ftell(file->fallback);
        return position >= 0 ? static_cast<size_t>(position) : 0;
    }
    return file->position;
}

size_t ClientRmlFileInterface::Length(Rml::FileHandle handle)
{
    auto* file = reinterpret_cast<RmlResourceFile*>(handle);
    if (!file) {
        return 0;
    }
    if (file->fallback) {
        return Rml::FileInterface::Length(handle);
    }
    return file->bytes.size();
}

nw::Resource ClientRmlFileInterface::resource_from_path(Rml::String path) const
{
    std::replace(path.begin(), path.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '|', ':');

    if (const auto protocol = path.find("://"); protocol != Rml::String::npos) {
        path.erase(0, protocol + 3);
    }
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    if (const auto query = path.find('?'); query != Rml::String::npos) {
        path.resize(query);
    }

    auto resource = nw::Resource::from_path(std::filesystem::path{path}, true);
    if (resource.valid() && ui_resources_->contains(resource)) {
        return resource;
    }

    if (!path.empty() && path.rfind("ui/", 0) != 0) {
        return nw::Resource::from_path(std::filesystem::path{"ui"} / path, true);
    }

    return resource;
}

nw::ResourceData ClientRmlFileInterface::demand(const Rml::String& path) const
{
    const nw::Resource resource = resource_from_path(path);
    if (!resource.valid()) {
        return {};
    }
    auto result = ui_resources_->demand(resource);
    if (result.bytes.size() || resource.type != nw::ResourceType::tga) {
        return result;
    }

    const auto filename = std::filesystem::path{resource.filename()}.filename();
    const auto game_resource = nw::Resource::from_path(filename);
    return game_resource.valid() ? game_resources_->demand(game_resource) : nw::ResourceData{};
}

} // namespace nw::toolset
