#include "Key.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/templates.hpp"

#include <nowide/convert.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace nw {

/// @cond NEVER
struct KeyHeader {
    char type[4];
    char version[4];
    uint32_t bif_count;
    uint32_t key_count;
    uint32_t offset_file_table;
    uint32_t offset_key_table;
    uint32_t year;
    uint32_t day_of_year;
    char reserved[32];
};

struct FileTable {
    uint32_t size;
    uint32_t name_offset;
    uint16_t name_size;
    uint16_t drives;
};
/// @endcond

namespace fs = std::filesystem;

Key::Key(fs::path path)
{
    if (!fs::exists(path)) {
        LOG_F(WARNING, "file '{}' does not exist", path);
        return;
    }

#ifdef _MSC_VER
    path_ = nowide::narrow(std::filesystem::canonical(path).c_str());
    name_ = nowide::narrow(path.filename().c_str());
#else
    path_ = std::filesystem::canonical(path);
    name_ = path.filename();
#endif

    is_loaded_ = load();
}

std::vector<ResourceDescriptor> Key::all() const
{
    std::vector<ResourceDescriptor> result;
    result.reserve(elements_.size());
    for (const auto& [k, v] : elements_) {
        result.push_back({k, bifs_[v.bif].elements[v.index].size, 0, this});
    }
    return result;
}

bool Key::contains(Resource res) const
{
    return elements_.find(res) != std::end(elements_);
}

ResourceData Key::demand(Resource res) const
{
    ResourceData data;
    if (!is_loaded_) {
        return data;
    }

    auto it = elements_.find(res);

    if (it == std::end(elements_)) {
        return data;
    } else if (it->second.bif >= bifs_.size() || it->second.index >= bifs_[it->second.bif].elements.size()) {
        return data;
    }

    data.name = res;
    data.bytes = bifs_[it->second.bif].demand(it->second.index);
    return data;
}

int Key::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    if (!std::filesystem::is_directory(output)) {
        // Needs to be create_directories to simplify usage.  Alternatively,
        // could assert that path must already exist.  Needs error handling.
        std::filesystem::create_directories(output);
    }

    int count = 0;
    std::string fname;
    ByteArray ba;
    for (const auto& [k, v] : elements_) {
        fname = k.filename();
        if (std::regex_match(fname, pattern)) {
            ++count;
            ba = bifs_[v.bif].demand(v.index);
            std::ofstream out{output / fs::path(fname), std::ios_base::binary};
            ostream_write(out, ba.data(), ba.size());
        }
    }
    return count;
}

size_t Key::size() const
{
    return elements_.size();
}

ResourceDescriptor Key::stat(const Resource& res) const
{
    ResourceDescriptor result;
    auto it = elements_.find(res);
    if (it != std::end(elements_)) {
        result.name = res;
        result.parent = this;
        result.size = bifs_[it->second.bif].elements[it->second.index].size;
        result.mtime = int64_t(fs::last_write_time(path_).time_since_epoch().count() / 1000);
    }
    return result;
}

void Key::visit(std::function<void(const Resource&)> callback) const noexcept
{
    for (auto it : elements_) {
        callback(it.first);
    }
}

// ---- Private ---------------------------------------------------------------

#define CHECK_OFFSET(offset)                                 \
    do {                                                     \
        if (static_cast<std::streamsize>(offset) > fsize_) { \
            LOG_F(ERROR, "corrupted key file");              \
            return false;                                    \
        }                                                    \
    } while (0)

bool Key::load()
{
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        LOG_F(ERROR, "{}: Unable to open file", path_);
        return false;
    }

    LOG_F(INFO, "{}: Loading...", path_);

    fsize_ = static_cast<std::streamsize>(fs::file_size(path_));

    KeyHeader header;
    CHECK_OFFSET(sizeof(KeyHeader));
    istream_read(file, &header, sizeof(KeyHeader));

    std::vector<FileTable> fts;
    fts.resize(header.bif_count);
    CHECK_OFFSET(header.offset_file_table);
    file.seekg(header.offset_file_table);
    CHECK_OFFSET(header.offset_file_table + sizeof(FileTable) * header.bif_count);
    istream_read(file, &fts[0], sizeof(FileTable) * header.bif_count);

    bifs_.reserve(header.bif_count);
    std::vector<std::string> bif_names;
    bif_names.reserve(header.bif_count);

    auto bif_path = fs::path(path_).parent_path().parent_path();
    for (const auto& it : fts) {
        char buffer[256] = {0};

        CHECK_OFFSET(it.name_offset + it.name_size);
        file.seekg(it.name_offset);
        file.read(buffer, it.name_size);

        std::string s(buffer);
        std::replace(s.begin(), s.end(), '\\', '/');

        LOG_F(INFO, "  {}: Loading...", s);
        bif_names.push_back(s);

        bifs_.emplace_back(this, bif_path / s);
    }

    file.seekg(header.offset_key_table);
    elements_.reserve(header.key_count);
    char buffer[17] = {0};
    for (size_t i = 0; i < header.key_count; ++i) {
        file.read(buffer, 16);

        uint32_t id;
        ResourceType::type type;
        istream_read(file, &type, 2);
        istream_read(file, &id, 4);
        elements_.emplace(Resource{std::string(buffer), type},
            KeyTableElement{id >> 20, id & 0xFFFFF});
    }

    LOG_F(INFO, "{}: {} resources loaded.", path_, elements_.size());
    return true;
}

#undef CHECK_OFFSET

} // namespace nw
