#include "Zip.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/macros.hpp"

#include <nowide/fstream.hpp>

#include <cstring>

namespace fs = std::filesystem;

namespace nw {

Zip::Zip(fs::path filename)
    : filename_{std::move(filename)}
{
    is_loaded_ = load();
}

Zip::~Zip()
{
    unzClose(file_);
}

std::vector<Resource> Zip::all()
{
    std::vector<Resource> res;
    res.reserve(elements_.size());
    for (const auto& it : elements_) {
        res.push_back(it.resref);
    }
    return res;
}

ByteArray Zip::demand(Resource resref)
{
    ByteArray ba;
    char fname[64] = {0};

    std::string fn = resref.filename();
    // The 2 below forces case insensitve comparison
    if (unzLocateFile(file_, fn.c_str(), 2) == UNZ_OK) {
        unzOpenCurrentFile(file_);
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        ba.resize(info.uncompressed_size);
        if (unzReadCurrentFile(file_, ba.data(), info.uncompressed_size) == 0) {
            // Not sure about the return here..
        }
        unzCloseCurrentFile(file_);
    }
    return ba;
}

int Zip::extract(const std::regex& pattern, const std::filesystem::path& output)
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
        if (dot && dot - fname <= 16) {
            Resource r(std::string_view(fname, dot - fname), ResourceType::from_extension(dot + 1));
            if (r.valid() && std::regex_match(fname, pattern)) {
                auto ba = demand(r);
                if (ba.size() > 0) {
                    nowide::ofstream out{(output / r.filename()).string(), std::ios_base::binary};
                    out.write((char*)ba.data(), ba.size());
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

ResourceDescriptor Zip::stat(const Resource& res)
{
    ResourceDescriptor rd;
    std::string fn = res.filename();
    char fname[64] = {0};
    if (unzLocateFile(file_, fn.c_str(), 0) == UNZ_OK) {
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        char* dot = strchr(fname, '.');
        if (dot && dot - fname <= 16) {
            rd.size = info.uncompressed_size;
            rd.name = Resource(std::string_view(fname, dot - fname), ResourceType::from_extension(dot + 1));
        }
    }
    return rd;
}

bool Zip::load()
{
    file_ = unzOpen(filename_.c_str());
    if (!file_) {
        LOG_F(ERROR, "zip unable to open {}", filename_.string());
        return false;
    }
    LOG_F(INFO, "{}: Loading...", filename_.string());

    unz_global_info gi;
    unzGetGlobalInfo(file_, &gi);

    char fname[64] = {0};
    int res = unzGoToFirstFile(file_);
    while (res == UNZ_OK) {
        ZipElement e;
        unz_file_info info;
        unzGetCurrentFileInfo(file_, &info, fname, 64, nullptr, 0, nullptr, 0);
        char* dot = strchr(fname, '.');
        if (dot && dot - fname <= 16) {
            e.size = info.uncompressed_size;
            e.resref = Resource(std::string_view(fname, dot - fname), ResourceType::from_extension(dot + 1));
            if (e.resref.valid())
                elements_.push_back(e);
        } else {
            LOG_F(INFO, "zip skipping {}", fname);
        }
        res = unzGoToNextFile(file_);
    }
    // TODO: Sort
    LOG_F(INFO, "{}: Loaded {} resource(s).", filename_.string(), elements_.size());
    return true;
}

} // namespace nw
