#include "Bif.hpp"

#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/templates.hpp"

#include <cstdint>

namespace nw {

namespace fs = std::filesystem;

struct BifHeader {
    char type[4];
    char version[4];
    uint32_t var_res_count;
    uint32_t fix_res_count; // Unused
    uint32_t var_table_offset;
};

Bif::Bif(Key* key, fs::path path)
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

} // namespace nw
