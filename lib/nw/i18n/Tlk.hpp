#pragma once

#include "../i18n/Language.hpp"
#include "../util/ByteArray.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nw {

/// Tlk Flags
struct TlkFlags {
    static constexpr uint32_t empty = 0x0;
    static constexpr uint32_t text = 0x1;
    static constexpr uint32_t sound = 0x2;
    static constexpr uint32_t sound_length = 0x4;
};

struct TlkHeader {
    std::array<char, 4> type = {'T', 'L', 'K', ' '};
    std::array<char, 4> version = {'V', '3', '.', '0'};
    uint32_t language_id = 0;
    uint32_t str_count = 0;
    uint32_t str_offset = 0;
};

struct TlkElement {
    TlkElement()
    {
        memset(this, 0, sizeof(TlkElement));
    }
    uint32_t flags;
    std::array<char, 16> sound;
    uint32_t unused[2];
    uint32_t offset;
    uint32_t size;
    float snd_duration;
};

struct Tlk {
    static constexpr uint32_t custom_flag = 0x01000000;

    explicit Tlk(LanguageID language = LanguageID::english);
    explicit Tlk(std::filesystem::path filename);
    Tlk(const Tlk&) = delete;
    Tlk(Tlk&&) = default;

    /// Get a localized string
    std::string get(uint32_t strref) const;

    /// Get language ID
    LanguageID language_id() const noexcept;

    /// Is Tlk modfied
    bool modified() const noexcept;

    /// Write TLK to file
    void save();

    /// Write TLK to file
    void save_as(const std::filesystem::path& path);

    /// Set a localized string
    void set(uint32_t strref, std::string_view string);

    /**
     * @brief Get the number of tlk entries
     * @note This is equivalent to the highest string reference, not the number of actual strings
     */
    size_t size() const noexcept;

    /// Get if successfully parsed
    bool valid() const noexcept;

    /// Get a localized string
    std::string operator[](uint32_t strref) const { return get(strref); };

    Tlk& operator=(const Tlk&) = delete;
    Tlk& operator=(Tlk&&) = default;

private:
    std::filesystem::path path_;
    ByteArray bytes_;
    TlkHeader header_;
    TlkElement* elements_ = nullptr;
    absl::flat_hash_map<uint32_t, std::string> modified_strings_;

    void load();
    bool loaded_ = false;
};

} // namespace nw
