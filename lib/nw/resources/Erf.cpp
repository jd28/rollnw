#include "Erf.hpp"

#include "../i18n/conversion.hpp"
#include "../log.hpp"
#include "../util/platform.hpp"
#include "../util/string.hpp"
#include "../util/templates.hpp"
#include "Resource.hpp"

#include "../util/templates.hpp"

#include <nowide/convert.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
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
struct ErfKey {
    std::array<char, 16> resref;
    uint32_t id;
    ResourceType::type type;
    int16_t unused;
};

Erf::Erf(const std::filesystem::path& path)
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

    is_loaded_ = load(path);
}

bool Erf::add(Resource res, const ByteArray& bytes)
{
    auto p = working_directory() / fs::path(res.filename());
    bytes.write_to(p);
    elements_[res] = p;
    return true;
}

bool Erf::add(const std::filesystem::path& path)
{
    Resource res = Resource::from_path(path);
    if (!res.valid()) {
        LOG_F(ERROR, "erf: attempting to add resource with invalid name '{}'.", path);
        return false;
    }
    auto p = working_directory() / fs::path(res.filename());
    fs::copy_file(path, working_directory() / fs::path(res.filename()), fs::copy_options::overwrite_existing);
    elements_[res] = p;
    return true;
}

size_t Erf::erase(const Resource& res)
{
    return elements_.erase(res);
}

bool Erf::merge(const Container* container)
{
    if (!container) {
        return false;
    }
    auto all = container->all();
    bool result = true;
    for (const auto& a : all) {
        result &= add(a.name, container->demand(a.name));
    }
    return result;
}

bool Erf::reload()
{
    file_.close();
    elements_.clear();
    return is_loaded_ = load(path());
}

bool Erf::save() const
{
    return save_as(fs::path(path_));
}

bool Erf::save_as(const std::filesystem::path& path) const
{
    ErfHeader header;
    memset(&header, 0, sizeof(ErfHeader));

    // Set type
    switch (type) {
    case ErfType::erf:
        memcpy(header.type, "ERF ", 4);
        break;
    case ErfType::hak:
        memcpy(header.type, "HAK ", 4);
        break;
    case ErfType::mod:
        memcpy(header.type, "MOD ", 4);
        break;
    case ErfType::sav:
        memcpy(header.type, "SAV ", 4);
        break;
    }

    // Set version
    memcpy(header.version, "V1.0", 4);

    // Set some easy stuff that requires no calculations
    header.entry_count = static_cast<uint32_t>(elements_.size());
    header.locstring_count = static_cast<uint32_t>(description.size());
    header.desc_strref = description.strref();

    // Set build date
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm local_tm = *localtime(&tt);
    header.day_of_year = static_cast<uint32_t>(local_tm.tm_yday);
    header.year = static_cast<uint32_t>(local_tm.tm_year + 1900);

    // Construct byte array of LocStrings
    ByteArray locstrings;
    for (const auto& [lang, str] : description) {
        locstrings.append(&lang, 4);
        auto base_lang = Language::to_base_id(lang);
        std::string tmp = from_utf8_by_langid(str, base_lang.first);
        uint32_t tmp_sz = static_cast<uint32_t>(tmp.size());
        locstrings.append(&tmp_sz, 4);
        locstrings.append(tmp.c_str(), tmp.size());
        if (type == ErfType::hak || type == ErfType::erf) {
            locstrings.push_back(0); // Null Terminated in HAK and ERF..
        }
    }
    header.locstring_size = static_cast<uint32_t>(locstrings.size());

    // Calculate up the offsets
    header.offset_locstring = sizeof(ErfHeader);
    header.offset_keys = header.offset_locstring + header.locstring_size;
    header.offset_res = header.offset_keys + static_cast<uint32_t>(header.entry_count * sizeof(ErfKey));
    uint32_t data_offset = header.offset_res + static_cast<uint32_t>(header.entry_count * sizeof(ErfElementInfo));

    std::vector<Resource> entries;
    std::vector<ErfKey> entry_keys;
    std::vector<ErfElementInfo> entry_info;
    entries.reserve(elements_.size());
    entry_keys.reserve(elements_.size());
    entry_info.reserve(elements_.size());

    for (auto& [k, v] : elements_) {
        entries.push_back(k);
    }

    // Sort everything so same erf saved twice produces byte identical results.
    std::sort(std::begin(entries), std::end(entries));
    uint32_t id = 0;
    for (const auto& e : entries) {
        auto rd = stat(e);
        entry_keys.push_back({e.resref.data(), id++, e.type, 0});
        entry_info.push_back({data_offset, static_cast<uint32_t>(rd.size)});
        data_offset += static_cast<uint32_t>(rd.size);
    }

    fs::path temp_path = fs::temp_directory_path() / path.filename();
    std::ofstream f{temp_path, std::ios::binary};
    if (!f.is_open()) {
        return false;
    }

    ostream_write(f, &header, sizeof(ErfHeader));
    ostream_write(f, locstrings.data(), locstrings.size());
    ostream_write(f, entry_keys.data(), entry_keys.size() * sizeof(ErfKey));
    ostream_write(f, entry_info.data(), entry_info.size() * sizeof(ErfElementInfo));

    ByteArray ba;
    for (const auto& e : entries) {
        ba = demand(e);
        ostream_write(f, ba.data(), ba.size());
    }
    f.close();
    return move_file_safely(temp_path, path);
}

std::vector<ResourceDescriptor> Erf::all() const
{
    std::vector<ResourceDescriptor> result;
    result.reserve(elements_.size());
    for (const auto& [k, v] : elements_) {
        result.push_back(stat(k));
    }
    return result;
}

bool Erf::contains(Resource res) const
{
    return elements_.find(res) != std::end(elements_);
}

ByteArray Erf::demand(Resource res) const
{
    if (!is_loaded_) {
        return {};
    }
    auto it = elements_.find(res);
    if (it == elements_.end()) {
        return {};
    }
    return read(it->second);
}

int Erf::extract(const std::regex& re, const std::filesystem::path& output) const
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
                std::ofstream out{output / fs::path(fname), std::ios_base::binary};
                ostream_write(out, ba.data(), ba.size());
            }
        }
    }
    return count;
}

size_t Erf::size() const
{
    return elements_.size();
}

ResourceDescriptor Erf::stat(const Resource& res) const
{
    ResourceDescriptor result;
    auto it = elements_.find(res);
    if (it != std::end(elements_)) {
        result.name = res;
        result.parent = this;
        if (std::holds_alternative<fs::path>(it->second)) {
            result.size = fs::file_size(std::get<fs::path>(it->second));
            result.mtime = int64_t(fs::last_write_time(std::get<fs::path>(it->second)).time_since_epoch().count() / 1000);
        } else {
            result.size = std::get<ErfElementInfo>(it->second).size;
            result.mtime = int64_t(fs::last_write_time(path_).time_since_epoch().count() / 1000);
        }
    }
    return result;
}

// ---- Private ---------------------------------------------------------------

#define CHECK_OFFSET(offset)                                 \
    do {                                                     \
        if (static_cast<std::streamsize>(offset) > fsize_) { \
            return false;                                    \
        }                                                    \
    } while (0)

bool Erf::load(const fs::path& path)
{
    LOG_F(INFO, "{}: Loading...", path);

    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        LOG_F(ERROR, "{}: Unable to open.", path);
        return false;
    }

    fsize_ = static_cast<std::streamsize>(fs::file_size(path));

    CHECK_OFFSET(sizeof(ErfHeader));

    ErfHeader header;
    istream_read(file_, &header, sizeof(ErfHeader));

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
        LOG_F(ERROR, "{}: Invalid header type.", path);
        return false;
    }

    if (strncmp(header.version, "V1.0", 4) == 0) {
        version = ErfVersion::v1_0;
    } else {
        LOG_F(ERROR, "{}: Invalid version type '{}'.", path, std::string_view{header.version, 4});
        return false;
    }

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
        std::string tmp;
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

    std::vector<ErfKey> keys;
    keys.resize(header.entry_count);
    file_.seekg(header.offset_keys, std::ios_base::beg);
    istream_read(file_, keys.data(), sizeof(ErfKey) * header.entry_count);

    std::vector<ErfElementInfo> info;
    info.resize(header.entry_count);
    file_.seekg(header.offset_res, std::ios_base::beg);
    istream_read(file_, info.data(), sizeof(ErfElementInfo) * header.entry_count);

    for (size_t i = 0; i < header.entry_count; ++i) {
        elements_.emplace(Resource{keys[i].resref, keys[i].type}, info[i]);
    }

    LOG_F(INFO, "{}: {} resources loaded.", path, elements_.size());
    return true;
}

#undef CHECK_OFFSET

ByteArray Erf::read(const ErfElement& e) const
{
    ByteArray ba;

    if (std::holds_alternative<std::filesystem::path>(e)) {
        return ByteArray::from_file(std::get<std::filesystem::path>(e));
    } else {
        auto& ele = std::get<ErfElementInfo>(e);
        if (ele.offset == std::numeric_limits<uint32_t>::max()) {
            // Do nothing, but I forget why.  Maybe part of the nwserver docker build.
        } else if (ele.offset + ele.size > fsize_) {
            LOG_F(ERROR, "{}: Out of bounds.", path_);
        } else {
            ba.resize(ele.size);
            file_.seekg(ele.offset, file_.beg);
            istream_read(file_, ba.data(), ele.size);
        }
    }
    return ba;
}

} // namespace nw
