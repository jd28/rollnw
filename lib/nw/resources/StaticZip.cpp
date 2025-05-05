#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/macros.hpp"
#include "../util/templates.hpp"
#include "StaticZip.hpp"

#include <nowide/convert.hpp>
#include <nowide/fstream.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <zip.h>

namespace fs = std::filesystem;

namespace nw {

StaticZip::StaticZip(const fs::path& path)
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

StaticZip::~StaticZip() = default;

bool StaticZip::load()
{
    int err = 0;
    zip_t* zip = zip_open(path_.c_str(), ZIP_RDONLY, &err);
    if (!zip) {
        LOG_F(ERROR, "zip unable to open {} (err={})", path_, err);
        return false;
    }

    zip_int64_t count = zip_get_num_entries(zip, 0);
    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
        zip_stat_t st;
        if (zip_stat_index(zip, i, 0, &st) != 0) { continue; }

        std::string_view full_name = st.name;
        if (full_name.empty() || full_name.back() == '/') { continue; }

        auto last_slash = full_name.find_last_of('/');
        auto last_dot = full_name.find_last_of('.');

        if (last_dot == std::string_view::npos || (last_slash != std::string_view::npos && last_dot < last_slash)) {
            continue;
        }

        std::string_view base = full_name.substr(last_slash + 1, last_dot - last_slash - 1);
        std::string_view ext = full_name.substr(last_dot + 1);

        if (base.size() > nw::kernel::config().max_resref_length()) {
            LOG_F(INFO, "[zip] skipping '{}' file name is greater than max resref length", full_name);
            continue;
        }

        auto r = Resource(base, ResourceType::from_extension(ext));
        if (r.valid()) {
            elements_.emplace_back(r, String(full_name), st.size, i);
        }
    }

    zip_close(zip);

    LOG_F(INFO, "{}: Loaded {} resource(s).", path_, elements_.size());
    return true;
}

ResourceData StaticZip::demand(const ContainerKey* key) const
{
    auto k = reinterpret_cast<const detail::StaticZipKey*>(key);
    if (!k) return {};

    ResourceData data;
    data.name = k->name;

    int err = 0;
    zip_t* zip = zip_open(path_.c_str(), ZIP_RDONLY, &err);
    if (!zip) {
        LOG_F(ERROR, "Unable to reopen zip '{}' (err={})", path_, err);
        return data;
    }

    zip_file_t* zf = zip_fopen_index(zip, k->index, 0);
    if (zf) {
        data.bytes.resize(k->size);
        if (zip_fread(zf, data.bytes.data(), k->size) < 0) {
            LOG_F(ERROR, "Failed to read file {} at index {}", k->path, k->index);
            data.bytes.clear();
        }
        zip_fclose(zf);
    } else {
        LOG_F(ERROR, "zip_fopen_index failed for {}", k->path);
    }

    zip_close(zip);
    return data;
}

size_t StaticZip::size() const
{
    return elements_.size();
}

const String& StaticZip::name() const { return name_; }
const String& StaticZip::path() const { return path_; }
bool StaticZip::valid() const noexcept { return is_loaded_; }

void StaticZip::visit(std::function<void(Resource, const ContainerKey*)> visitor) const
{
    for (const auto& it : elements_) {
        visitor(it.name, reinterpret_cast<const ContainerKey*>(&it));
    }
}

} // namespace nw
