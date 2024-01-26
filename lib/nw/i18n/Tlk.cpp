#include "Tlk.hpp"

#include "../i18n/Language.hpp"
#include "../i18n/conversion.hpp"
#include "../log.hpp"
#include "../util/macros.hpp"
#include "../util/platform.hpp"
#include "../util/templates.hpp"

#include <fstream>

namespace fs = std::filesystem;

namespace nw {

Tlk::Tlk(LanguageID language)
{
    header_.language_id = static_cast<uint32_t>(language);
    loaded_ = true;
}

Tlk::Tlk(std::filesystem::path filename)
    : path_{std::move(filename)}
{
    load();
}

std::string Tlk::get(uint32_t strref) const
{
    std::string result;
    if (!loaded_) {
        LOG_F(ERROR, "attempting to get string from invalid tlk");
        return result;
    }

    auto it = modified_strings_.find(strref);
    if (it != std::end(modified_strings_)) {
        return it->second;
    }

    if (strref > header_.str_count || !elements_) {
        LOG_F(ERROR, "attempting to get string from invalid strref");
        return result;
    }

    const auto& ele = elements_[strref];
    if (header_.str_offset + ele.offset + ele.size <= bytes_.size()) {
        const char* temp = reinterpret_cast<const char*>(bytes_.data() + header_.str_offset + ele.offset);
        std::string s = string::sanitize_colors({temp, ele.size});
        result = to_utf8_by_langid(s, language_id());
    } else {
        LOG_F(ERROR, "failed to read strref: {}", strref);
    }

    return result;
}

LanguageID Tlk::language_id() const noexcept
{
    return static_cast<LanguageID>(header_.language_id);
}

bool Tlk::modified() const noexcept
{
    return modified_strings_.size() > 0;
}

void Tlk::set(uint32_t strref, std::string_view string)
{
    modified_strings_[strref] = std::string(string);
}

size_t Tlk::size() const noexcept
{
    return header_.str_count;
}

bool Tlk::valid() const noexcept
{
    return loaded_;
}

void Tlk::save()
{
    save_as(path_);
}

void Tlk::save_as(const std::filesystem::path& path)
{
    uint32_t max_strref = header_.str_count;
    for (const auto& [k, v] : modified_strings_) {
        max_strref = std::max(max_strref, k);
    }

    std::vector<TlkElement> ele;
    ele.resize(max_strref);

    uint32_t orig_str_offset = header_.str_offset;
    header_.str_offset = static_cast<uint32_t>(sizeof(TlkHeader) + (ele.size() * sizeof(TlkElement)));
    uint32_t offset = 0;
    std::string tmp;
    ByteArray strings;

    for (uint32_t i = 0; i < max_strref; ++i) {
        auto it = modified_strings_.find(i);
        if (it != std::end(modified_strings_)) {
            ele[i].flags = TlkFlags::text;
            ele[i].offset = offset;
            tmp = from_utf8_by_langid(it->second, language_id());
            ele[i].size = static_cast<uint32_t>(tmp.size());
            offset += ele[i].size;
        } else if (i < header_.str_count) {
            if (TlkFlags::text & elements_[i].flags) {
                ele[i].flags |= TlkFlags::text;
                ele[i].offset = offset;
                ele[i].size = elements_[i].size;
                offset += ele[i].size;
            }
            if (TlkFlags::sound & elements_[i].flags) {
                ele[i].flags |= TlkFlags::sound;
                ele[i].sound = elements_[i].sound;
            }
            if (TlkFlags::sound_length & elements_[i].flags) {
                ele[i].flags |= TlkFlags::sound_length;
                ele[i].snd_duration = elements_[i].snd_duration;
            }
        }
    }

    fs::path temp_path = fs::temp_directory_path() / path.filename();
    std::ofstream f{temp_path, std::ios::binary};

    header_.str_count = max_strref;
    ostream_write(f, &header_, sizeof(TlkHeader));
    ostream_write(f, ele.data(), ele.size() * sizeof(TlkElement));

    for (uint32_t i = 0; i < max_strref; ++i) {
        auto it = modified_strings_.find(i);
        if (it != std::end(modified_strings_)) {
            tmp = from_utf8_by_langid(it->second, language_id());
            ostream_write(f, it->second.data(), it->second.size());
        } else if (i < header_.str_count) {
            if (TlkFlags::text & elements_[i].flags) {
                ostream_write(f, bytes_.data() + orig_str_offset + elements_[i].offset, elements_[i].size);
            }
        }
    }

    f.close();
    if (move_file_safely(temp_path, path)) {
        path_ = path;
        load(); // Maybe reloading isn't necessary..
    }
}

void Tlk::load()
{
    bytes_ = ByteArray::from_file(path_);
    if (modified_strings_.size()) {
        modified_strings_.clear();
    }

#define CHECK_OFF(value, msg)                                                                                              \
    do {                                                                                                                   \
        if (!(value)) {                                                                                                    \
            throw std::runtime_error(fmt::format("corrupt tlk: {}, error: {} ({})", path_, msg, ROLLNW_STRINGIFY(value))); \
        }                                                                                                                  \
    } while (0)

    CHECK_OFF(bytes_.size() > sizeof(TlkHeader), "invalid header");
    memcpy(&header_, bytes_.data(), sizeof(TlkHeader));
    CHECK_OFF(strncmp(header_.type.data(), "TLK ", 4) == 0, "invalid format type");
    CHECK_OFF(strncmp(header_.version.data(), "V3.0", 4) == 0, "invalid format version");
    elements_ = reinterpret_cast<TlkElement*>(bytes_.data() + sizeof(TlkHeader));
    CHECK_OFF(bytes_.size() >= sizeof(TlkElement) * header_.str_count + sizeof(TlkHeader),
        "strings corrupted");

    loaded_ = true;

#undef CHECK_OFF
}

} // namespace nw
