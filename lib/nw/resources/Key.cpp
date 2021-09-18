#include "Key.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
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
    : path_(std::move(path))
{
    is_loaded_ = load();
}

ByteArray Key::demand(Resource res)
{
    ByteArray ba;
    if (!is_loaded_) { return ba; }

    auto it = elements_.find(res);

    if (it == std::end(elements_)) {
        return ba;
    } else if (it->second.bif >= bifs_.size() || it->second.index >= bifs_[it->second.bif].elements.size()) {
        return ba;
    }

    return bifs_[it->second.bif].demand(it->second.index);
}

int Key::extract(const std::regex& pattern, const std::filesystem::path& output)
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
            std::ofstream out{output / fname};
            out.write((char*)ba.data(), ba.size());
        }
    }
    return count;
}

size_t Key::size() const
{
    return elements_.size();
}

// ---- Private ---------------------------------------------------------------

#define CHECK_OFFSET(offset)                                 \
    do {                                                     \
        if (static_cast<std::streamsize>(offset) > fsize_) { \
            return false;                                    \
        }                                                    \
    } while (0)

bool Key::load()
{
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        LOG_F(ERROR, "{}: Unable to open file", path_.c_str());
        return false;
    }

    LOG_F(INFO, "{}: Loading...", path_.c_str());

    fsize_ = fs::file_size(path_);

    KeyHeader header;
    CHECK_OFFSET(sizeof(KeyHeader));
    file.read((char*)&header, sizeof(KeyHeader));

    std::vector<FileTable> fts;
    fts.resize(header.bif_count);
    CHECK_OFFSET(header.offset_file_table);
    file.seekg(header.offset_file_table);
    CHECK_OFFSET(header.offset_file_table + sizeof(FileTable) * header.bif_count);
    file.read((char*)&fts[0], sizeof(FileTable) * header.bif_count);

    bifs_.reserve(header.bif_count);
    std::vector<std::string> bif_names;
    bif_names.reserve(header.bif_count);

    for (const auto& it : fts) {
        char buffer[256] = {0};

        CHECK_OFFSET(it.name_offset + it.name_size);
        file.seekg(it.name_offset);
        file.read(buffer, it.name_size);

        std::string s(buffer);
        std::replace(s.begin(), s.end(), '\\', '/');

        LOG_F(INFO, "  {}: Loading...", s);
        bif_names.push_back(s);
        bifs_.emplace_back(this, path_.parent_path().parent_path() / s);
    }

    file.seekg(header.offset_key_table);
    elements_.reserve(header.key_count);
    char buffer[17] = {0};
    for (size_t i = 0; i < header.key_count; ++i) {
        file.read(buffer, 16);

        uint32_t id;
        ResourceType::type type;
        file.read((char*)&type, 2);
        file.read((char*)&id, 4);
        elements_.emplace(Resource{std::string(buffer), type},
            KeyTableElement{id >> 20, id & 0xFFFFF});
    }

    LOG_F(INFO, "{}: {} resources loaded.", path_.c_str(), elements_.size());
    return true;
}

#undef CHECK_OFFSET

} // namespace nw
