#pragma once

#include "../util/ByteArray.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nw {

/// Tlk Flags
enum class TlkFlags : uint32_t {
    text = 0x1,
    snd = 0x2,
    sndlength = 0x4
};

namespace detail {

/// @cond NEVER
struct TlkHeader {
    char type[4];
    char version[4];
    uint32_t language_id;
    uint32_t str_count;
    uint32_t str_offset;
};

struct TlkElement {
    TlkFlags flags;
    char snd_resref[16];
    uint32_t unused[2];
    uint32_t offset;
    uint32_t size;
    float snd_duration;
};
/// @endcond

} // namespace detail

struct Tlk {
    explicit Tlk(const std::filesystem::path& filename);
    Tlk(const Tlk&) = delete;
    Tlk(Tlk&&) = default;

    /// Get a localized string
    std::string_view get(uint32_t strref);

    /**
     * @brief Get the number of tlk entries
     * @note This is equivalent to the highest string reference, not the number of actual strings
     */
    size_t size() const noexcept;

    /// Get if successfully parsed
    bool is_valid() const noexcept;

    Tlk& operator=(const Tlk&) = default;
    Tlk& operator=(Tlk&&) = default;

private:
    ByteArray bytes_;
    detail::TlkHeader* header_ = nullptr;
    detail::TlkElement* elements_ = nullptr;
    bool loaded_ = false;
};

} // namespace nw
