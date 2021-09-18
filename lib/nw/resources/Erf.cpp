// Open Issues:
// 1. Currently the implementation here uses a flatmap and binary search to locate
//    resources.
// 2. The question as to whether or not to throw exceptions.
// 3. Some functions are logically const but due needing to modify the ifstream
//    they cannot be const.  Marking that a mutable and some
//    file locking is an option.
// 4. Whether mmaping is worth it, probably not.

#include "Erf.hpp"

#include "../log.hpp"
#include "../util/string.hpp"
#include "Resource.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace nw {

struct ErfHeader {
    char type[4];
    char version[4];
    uint32_t locstring_count;
    uint32_t locstring_size;
    uint32_t entry_count;
    uint32_t offset_locstring;
    uint32_t offset_keys;
    uint32_t offset_res;
    uint32_t year;
    uint32_t day_of_year;
    uint32_t desc_strref;
    char reserved[116];
};

struct ErfKey {
    char resref[16];
    uint32_t id;
    uint16_t type;
    int16_t unused;
};

Erf::Erf(std::filesystem::path path)
    : path_(std::move(path))
{
    is_loaded_ = load();
}

ByteArray Erf::read(const ErfElement& e)
{
    ByteArray ba;

    if (e.info.offset == -1u) {
        // Do nothing, but I forget why.  Maybe part of the nwserver docker build.
    } else if (e.info.offset + e.info.size > fsize_) {
        LOG_F(ERROR, "{}: Out of bounds.", path_.c_str());
    } else {
        ba.resize(e.info.size);
        file_.seekg(e.info.offset, std::ios_base::beg);
        file_.read((char*)ba.data(), e.info.size);
    }

    return ba;
}

ByteArray Erf::demand(Resource res)
{
    if (!is_loaded_) { return {}; }
    auto it = elements_.find(res);
    if (it == elements_.end()) { return {}; }
    return read(it->second);
}

int Erf::extract(std::string_view glob, const std::filesystem::path& output)
{
    return extract(string::glob_to_regex(glob, true), output);
}

int Erf::extract(const std::regex& re, const std::filesystem::path& output)
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
        if (std::regex_match(fname, re)) {
            ba = read(v);
            if (ba.size()) {
                ++count;
                std::ofstream out{output / fname};
                out.write((char*)ba.data(), ba.size());
            }
        }
    }
    return count;
}

size_t Erf::size() const
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

bool Erf::load()
{
    LOG_F(INFO, "{}: Loading...", path_.c_str());

    file_.open(path_.c_str(), std::ios::in | std::ios::binary);
    if (!file_.is_open()) {
        LOG_F(ERROR, "{}: Unable to open.", path_.c_str());
        return false;
    }

    fsize_ = fs::file_size(path_);

    CHECK_OFFSET(sizeof(ErfHeader));

    ErfHeader header;
    file_.read((char*)&header, sizeof(ErfHeader));

    CHECK_OFFSET(header.offset_keys + header.entry_count * sizeof(ErfKey));
    CHECK_OFFSET(header.offset_res + header.entry_count * sizeof(ErfElementInfo));

    if (strncmp(header.type, "MOD", 3) == 0) {
        type = ErfType::mod;
    } else if (strncmp(header.type, "HAK", 3) == 0) {
        type = ErfType::hak;
    } else if (strncmp(header.type, "ERF", 3) == 0) {
        type = ErfType::erf;
    } else if (strncmp(header.type, "SAV", 3) == 0) {
        type = ErfType::sav;
    } else {
        LOG_F(ERROR, "{}: Invalid header type.", path_.c_str());
        return false;
    }

    if (strncmp(header.version, "V1.0", 4) == 0) {
        version = ErfVersion::v1_0;
    } else {
        LOG_F(ERROR, "{}: Invalid version type '{}'.", path_.c_str(), std::string_view{header.version, 4});
        return false;
    }

    elements_.reserve(header.entry_count);

    uint32_t index;
    size_t entry_offset = header.offset_keys;
    size_t res_offset = header.offset_res;
    for (size_t i = 0; i < header.entry_count; ++i) {
        char buffer[17] = {0};
        ResourceType::type type;

        // I think that all ERF packers the key entry and the corresponding info are kept in the
        // same order.. but who knows.  Maybe not?

        file_.seekg(entry_offset, std::ios_base::beg);
        file_.read(buffer, 16);
        file_.read((char*)&index, 4);
        file_.read((char*)&type, 2);
        entry_offset += 16 + 8;

        ErfElement ele;
        file_.seekg(res_offset, std::ios_base::beg);
        file_.read((char*)&ele.info, sizeof(ErfElementInfo));
        res_offset += sizeof(ErfElementInfo);

        // [TODO] Change this to not use std::string.. Maybe
        elements_.emplace(Resource{std::string(buffer), type}, ele);
    }

    LOG_F(INFO, "{}: {} resources loaded.", path_.c_str(), elements_.size());
    return true;
}

#undef CHECK_OFFSET

} // namespace nw
