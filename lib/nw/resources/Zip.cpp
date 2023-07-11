#include "Zip.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/macros.hpp"
#include "../util/templates.hpp"

#include <nowide/convert.hpp>
#include <nowide/fstream.hpp>

#include <cstring>
#include <stdexcept>

namespace fs = std::filesystem;

namespace nw {

Zip::Zip(const fs::path& path)
{
    if (!fs::exists(path)) {
        LOG_F(WARNING, "file '{}' does not exist", path);
    }

#if _MSC_VER
    path_ = nowide::narrow(std::filesystem::canonical(path).c_str());
    name_ = nowide::narrow(path.filename().c_str());
#else
    path_ = std::filesystem::canonical(path);
    name_ = path.filename();
#endif

    is_loaded_ = load();
}

Zip::~Zip()
{
    unzClose(file_);
}

std::vector<ResourceDescriptor> Zip::all() const
{
    std::vector<ResourceDescriptor> res;
    res.reserve(elements_.size());
    for (const auto& it : elements_) {
        res.push_back({it.resref, it.size, 0, this});
    }
    return res;
}

bool Zip::contains(Resource res) const
{
    return !!stat(res);
}

ResourceData Zip::demand(Resource resref) const
{
    ResourceData data;
    data.name = resref;

    char fname[64] = {0};

    std::string fn = resref.filename();
    // The 2 below forces case insensitve comparison
    if (unzLocateFile(file_, fn.c_str(), 2) == UNZ_OK) {
        unzOpenCurrentFile(file_);
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        data.bytes.resize(info.uncompressed_size);
        if (unzReadCurrentFile(file_, data.bytes.data(), static_cast<uint32_t>(info.uncompressed_size)) == 0) {
            // Not sure about the return here..
        }
        unzCloseCurrentFile(file_);
    }
    return data;
}

int Zip::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    if (!std::filesystem::is_directory(output)) {
        // Needs to be create_directories to simplify usage.  Alternatively,
        // could assert that path must already exist.  Needs error handling.
        std::filesystem::create_directories(output);
    }

    int extracted = 0;
    char fname[64] = {0};
    int res = unzGoToFirstFile(file_);
    while (res == UNZ_OK) {
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        char* dot = strchr(fname, '.');
        if (dot && static_cast<size_t>(dot - fname) <= 16) {
            Resource r(std::string_view(fname, static_cast<size_t>(dot - fname)), ResourceType::from_extension(dot + 1));
            if (r.valid() && std::regex_match(fname, pattern)) {
                auto data = demand(r);
                if (data.bytes.size() > 0) {
                    nowide::ofstream out{(output / r.filename()).string(), std::ios_base::binary};
                    ostream_write(out, data.bytes.data(), data.bytes.size());
                    ++extracted;
                }
            }
        }
        res = unzGoToNextFile(file_);
    }

    return extracted;
}

size_t Zip::size() const
{
    return elements_.size();
}

ResourceDescriptor Zip::stat(const Resource& res) const
{
    ResourceDescriptor rd;
    std::string fn = res.filename();
    char fname[64] = {0};
    if (unzLocateFile(file_, fn.c_str(), 0) == UNZ_OK) {
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        char* dot = strchr(fname, '.');
        if (dot && static_cast<size_t>(dot - fname) <= 16) {
            rd.size = info.uncompressed_size;
            rd.name = Resource(std::string_view(fname, static_cast<size_t>(dot - fname)), ResourceType::from_extension(dot + 1));
            rd.parent = this;
        }
    }
    return rd;
}

void Zip::visit(std::function<void(const Resource&)> callback) const noexcept
{
    for (const auto& it : elements_) {
        callback(it.resref);
    }
}

bool Zip::load()
{
    file_ = unzOpen(path_.c_str());
    if (!file_) {
        LOG_F(ERROR, "zip unable to open {}", path_);
        return false;
    }
    LOG_F(INFO, "{}: Loading...", path_);

    unz_global_info gi;
    unzGetGlobalInfo(file_, &gi);

    char fname[64] = {0};
    int res = unzGoToFirstFile(file_);
    while (res == UNZ_OK) {
        ZipElement e;
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        char* dot = strchr(fname, '.');
        if (dot && static_cast<size_t>(dot - fname) <= 16) {
            e.size = info.uncompressed_size;
            e.resref = Resource(std::string_view(fname, static_cast<size_t>(dot - fname)), ResourceType::from_extension(dot + 1));
            if (e.resref.valid())
                elements_.push_back(e);
        } else {
            LOG_F(INFO, "zip skipping {}", fname);
        }
        res = unzGoToNextFile(file_);
    }
    // TODO: Sort
    LOG_F(INFO, "{}: Loaded {} resource(s).", path_, elements_.size());
    return true;
}

} // namespace nw
