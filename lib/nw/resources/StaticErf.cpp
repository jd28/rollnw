#include "StaticErf.hpp"

#include "../i18n/conversion.hpp"
#include "../log.hpp"
#include "../util/templates.hpp"

#include <nowide/convert.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <tuple>

namespace fs = std::filesystem;

namespace nw {

/// @private
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

/// @private
template <size_t N>
struct ErfKey {
    std::array<char, N> resref;
    uint32_t id;
    ResourceType::type type;
    int16_t unused;
};

StaticErf::StaticErf(const std::filesystem::path& path)
{
    if (!fs::exists(path)) {
        LOG_F(WARNING, "file '{}' does not exist", path);
        return;
    }

#ifdef _MSC_VER
    path_ = nowide::narrow(std::filesystem::canonical(path).c_str());
    name_ = nowide::narrow(path.filename().c_str());
#else
    path_ = std::filesystem::canonical(path).string();
    name_ = path.filename().string();
#endif

    is_loaded_ = load(path);
}

ResourceData StaticErf::demand(const ContainerKey* key) const
{
    auto k = reinterpret_cast<const detail::ErfKey*>(key);
    if (!k) { return {}; }

    ResourceData result = read(k);
    return result;
}

void StaticErf::visit(std::function<void(Resource, const ContainerKey*)> visitor) const
{
    for (const auto& it : elements_) {
        visitor(it.name, reinterpret_cast<const ContainerKey*>(&it));
    }
}

size_t StaticErf::size() const
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

bool StaticErf::load(const fs::path& path)
{
    LOG_F(INFO, "[resources] erf: loading - '{}'", path);
    std::ifstream file_;
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        LOG_F(ERROR, "{}: Unable to open.", path);
        return false;
    }

    fsize_ = static_cast<std::streamsize>(fs::file_size(path));

    CHECK_OFFSET(sizeof(ErfHeader));

    ErfHeader header;
    istream_read(file_, &header, sizeof(ErfHeader));

    if (strncmp(header.type, "MOD", 3) == 0) {
        type = ErfType::mod;
    } else if (strncmp(header.type, "HAK", 3) == 0) {
        type = ErfType::hak;
    } else if (strncmp(header.type, "ERF", 3) == 0) {
        type = ErfType::erf;
    } else if (strncmp(header.type, "SAV", 3) == 0) {
        type = ErfType::sav;
    } else {
        LOG_F(ERROR, "{}: Invalid header type.", path);
        return false;
    }

    if (strncmp(header.version, "V1.0", 4) == 0) {
        version = ErfVersion::v1_0;
    } else if (strncmp(header.version, "V1.1", 4) == 0) {
        version = ErfVersion::v1_1;
    } else {
        LOG_F(ERROR, "{}: Invalid version type '{}'.", path, StringView{header.version, 4});
        return false;
    }

    if (version == ErfVersion::v1_0) {
        CHECK_OFFSET(header.offset_keys + header.entry_count * sizeof(ErfKey<16>));
    } else if (version == ErfVersion::v1_1) {
        CHECK_OFFSET(header.offset_keys + header.entry_count * sizeof(ErfKey<32>));
    }

    CHECK_OFFSET(header.offset_res + header.entry_count * sizeof(ErfElementInfo));

    // It's not totally clear if `nwhak.exe` can have anything but English in the description.
    // The ERF file format saves LocStrings uniquely, and differently between types.
    // Per Bioware docs (might have changed with EE?):
    //     In .erf and .hak files, the String portion of the structure is a NULL-terminated
    //     character string. In .mod files, the String portion of the structure is a
    //     non-NULL-terminated character string. Consequently, when reading the String,
    //     a program should rely on the StringSize, not on the presence of a null terminator.
    description = LocString(header.desc_strref);
    CHECK_OFFSET(header.offset_locstring);
    file_.seekg(header.offset_locstring, std::ios::beg);
    for (uint32_t i = 0; i < header.locstring_count; ++i) {
        String tmp;
        uint32_t lang, sz;
        CHECK_OFFSET(int(file_.tellg()) + 8);
        istream_read(file_, &lang, 4);
        istream_read(file_, &sz, 4);
        CHECK_OFFSET(size_t(file_.tellg()) + sz);
        tmp.resize(sz + 1, 0);
        file_.read(tmp.data(), sz);
        auto base_lang = Language::to_base_id(lang);
        tmp = to_utf8_by_langid(tmp.c_str(), base_lang.first);
        description.add(base_lang.first, tmp, base_lang.second);
    }

    elements_.reserve(header.entry_count);

    if (version == ErfVersion::v1_0) {
        Vector<ErfKey<16>> keys;
        keys.resize(header.entry_count);
        file_.seekg(header.offset_keys, std::ios_base::beg);
        istream_read(file_, keys.data(), sizeof(ErfKey<16>) * header.entry_count);

        Vector<ErfElementInfo> info;
        info.resize(header.entry_count);
        file_.seekg(header.offset_res, std::ios_base::beg);
        istream_read(file_, info.data(), sizeof(ErfElementInfo) * header.entry_count);

        for (size_t i = 0; i < header.entry_count; ++i) {
            auto r = Resource{keys[i].resref, keys[i].type};
            elements_.emplace_back(r, info[i].offset, info[i].size);
        }
    } else if (version == ErfVersion::v1_1) {
        Vector<ErfKey<32>> keys;
        keys.resize(header.entry_count);
        file_.seekg(header.offset_keys, std::ios_base::beg);
        istream_read(file_, keys.data(), sizeof(ErfKey<32>) * header.entry_count);

        Vector<ErfElementInfo> info;
        info.resize(header.entry_count);
        file_.seekg(header.offset_res, std::ios_base::beg);
        istream_read(file_, info.data(), sizeof(ErfElementInfo) * header.entry_count);

        for (size_t i = 0; i < header.entry_count; ++i) {
            auto r = Resource{keys[i].resref, keys[i].type};
            elements_.emplace_back(r, info[i].offset, info[i].size);
        }
    }

    LOG_F(INFO, "[resources] erf: loaded {} resources - '{}'", elements_.size(), path);
    return true;
}

#undef CHECK_OFFSET

ResourceData StaticErf::read(const detail::ErfKey* ele) const
{
    ResourceData data;

    if (ele->offset == std::numeric_limits<uint32_t>::max()) {
        // Do nothing, but I forget why.  Maybe part of the nwserver docker build.
        return data;
    }

    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        LOG_F(ERROR, "[erf] failed to open file at '{}', path");
        return data;
    }

    stream.seekg(ele->offset);
    if (!stream) {
        LOG_F(ERROR, "[erf] failed reading asset from '{}': file offset out of bounds.", path_);
        return data;
    }

    data.bytes.resize(ele->size);
    istream_read(stream, data.bytes.data(), ele->size);

    return data;
}

} // namespace nw
