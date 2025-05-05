#include "StaticKey.hpp"

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

namespace fs = std::filesystem;

namespace nw {

namespace detail {

struct BifHeader {
    char type[4];
    char version[4];
    uint32_t var_res_count;
    uint32_t fix_res_count; // Unused
    uint32_t var_table_offset;
};

Bif::Bif(StaticKey* key, fs::path path)
    : key_(key)
    , path_(std::move(path))
{
    is_loaded_ = load();
}

#define CHECK_OFFSET(offset)                                 \
    do {                                                     \
        if (static_cast<std::streamsize>(offset) > fsize_) { \
            LOG_F(ERROR, "corrupted bif file");              \
            return false;                                    \
        }                                                    \
    } while (0)

bool Bif::load()
{
    if (!key_) {
        LOG_F(ERROR, "Invalid Key file");
        return false;
    }

    if (!fs::exists(path_)) { LOG_F(ERROR, "File '{}' does not exist.", path_); }
    file_.open(path_, std::ios_base::binary);
    if (!file_.is_open()) { LOG_F(ERROR, "Unable to open '{}'", path_); }

    fsize_ = static_cast<std::streamsize>(fs::file_size(path_));

    BifHeader header;
    CHECK_OFFSET(sizeof(BifHeader));
    istream_read(file_, &header, sizeof(BifHeader));

    uint32_t offset = header.var_table_offset;
    CHECK_OFFSET(header.var_table_offset);
    file_.seekg(offset, std::ios_base::beg);
    elements.resize(header.var_res_count);

    CHECK_OFFSET(header.var_table_offset + sizeof(BifElement) * header.var_res_count);
    istream_read(file_, &elements[0], sizeof(BifElement) * header.var_res_count);

    return true;
}

ByteArray Bif::demand(size_t index) const
{
    ByteArray ba;

    if (index >= elements.size()) {
        LOG_F(ERROR, "{}: Invalid index: {}", path_, index);
    } else if (elements[index].offset + elements[index].size > fsize_) {
        LOG_F(ERROR, "{}: Invalid offset {} is greater than file size: {}", path_,
            elements[index].offset + elements[index].size, fsize_);
    } else {
        ba.resize(elements[index].size);
        file_.seekg(elements[index].offset, std::ios_base::beg);
        istream_read(file_, ba.data(), elements[index].size);
    }

    return ba;
}

#undef CHECK_OFFSET

}

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

StaticKey::StaticKey(fs::path path)
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

ResourceData StaticKey::demand(const ContainerKey* key) const
{
    if (!is_loaded_) { return {}; }

    auto k = static_cast<const detail::StaticKeyKey*>(key);
    if (!k) { return {}; }

    ResourceData data;
    data.name = k->name;
    data.bytes = bifs_[k->element.bif].demand(k->element.index);
    return data;
}

int StaticKey::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    if (!std::filesystem::is_directory(output)) {
        // Needs to be create_directories to simplify usage.  Alternatively,
        // could assert that path must already exist.  Needs error handling.
        std::filesystem::create_directories(output);
    }

    int count = 0;
    String fname;
    ByteArray ba;
    for (const auto& it : elements_) {
        fname = it.name.filename();
        if (std::regex_match(fname, pattern)) {
            ++count;
            ba = bifs_[it.element.bif].demand(it.element.index);
            std::ofstream out{output / fs::path(fname), std::ios_base::binary};
            ostream_write(out, ba.data(), ba.size());
        }
    }
    return count;
}

size_t StaticKey::size() const
{
    return elements_.size();
}

void StaticKey::visit(std::function<void(Resource, const ContainerKey*)> visitor) const
{
    for (const auto& it : elements_) {
        visitor(it.name, reinterpret_cast<const ContainerKey*>(&it));
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

bool StaticKey::load()
{
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        LOG_F(ERROR, "{}: Unable to open file", path_);
        return false;
    }

    LOG_F(INFO, "[resources] key: loading - '{}'", path_);

    fsize_ = static_cast<std::streamsize>(fs::file_size(path_));

    KeyHeader header;
    CHECK_OFFSET(sizeof(KeyHeader));
    istream_read(file, &header, sizeof(KeyHeader));

    Vector<FileTable> fts;
    fts.resize(header.bif_count);
    CHECK_OFFSET(header.offset_file_table);
    file.seekg(header.offset_file_table);
    CHECK_OFFSET(header.offset_file_table + sizeof(FileTable) * header.bif_count);
    istream_read(file, &fts[0], sizeof(FileTable) * header.bif_count);

    bifs_.reserve(header.bif_count);
    Vector<String> bif_names;
    bif_names.reserve(header.bif_count);

    auto bif_path = fs::path(path_).parent_path().parent_path();
    for (const auto& it : fts) {
        char buffer[256] = {0};

        CHECK_OFFSET(it.name_offset + it.name_size);
        file.seekg(it.name_offset);
        file.read(buffer, it.name_size);

        String s(buffer);
        std::replace(s.begin(), s.end(), '\\', '/');

        LOG_F(INFO, "[resources] key: added bif - {}", s);
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
        auto r = Resource{String(buffer), type};
        elements_.emplace_back(r, detail::KeyTableElement{id >> 20, id & 0xFFFFF});
    }

    LOG_F(INFO, "[resources] key: loaded {} resources - '{}'", elements_.size(), path_);
    return true;
}

#undef CHECK_OFFSET

} // namespace nw
