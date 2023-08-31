#pragma once

#include <fmt/format.h>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace nw {

/**
 * @brief ``nw::Resref`` names a resource.
 *
 * In NWN1/EE they are 16 character arrays, in NWN2 32 character arrays.
 * These character arrays are case-insenstive.
 *
 * Later evolutions of resrefs, in Dragon Age, were 32 utf16 character arrays; then ultimately
 * seem to have been replaced by a combination of FNV hashes.
 *
 * Currently only the NWN1/EE variety is supported.
 */
struct Resref {
    static constexpr size_t max_size = 32;
    using Storage = std::array<char, max_size>;
    using value_type = typename Storage::value_type;
    using size_type = typename Storage::size_type;

    Resref();
    Resref(const Resref&) = default;

    template <size_t N>
    Resref(std::array<char, N>& string) noexcept;

    Resref(const char* string) noexcept;
    Resref(std::string_view string) noexcept;

    Resref& operator=(const Resref&) = default;

    /// Get underlying storage.
    const Storage& data() const noexcept;

    /// Checks if the underlying array has no non-null characters
    bool empty() const noexcept;

    /// Returns the number of char elements in the array, excluding nulls.
    size_type length() const noexcept;

    /// Creates ``std::string`` of underlying array
    std::string string() const;

    /// Creates ``std::string_view`` of underlying array without null padding
    std::string_view view() const noexcept;

private:
    Storage data_;
};

template <size_t N>
Resref::Resref(std::array<char, N>& string) noexcept
{
    static_assert(N <= Resref::max_size);
    memcpy(data_.data(), string.data(), N);
    std::transform(data_.begin(), data_.end(), data_.begin(), ::tolower);
}

inline bool operator==(const Resref& lhs, const Resref& rhs)
{
    return lhs.view() == rhs.view();
}

inline bool operator<(const Resref& lhs, const Resref& rhs)
{
    return lhs.view() < rhs.view();
}

std::ostream& operator<<(std::ostream& out, const Resref& resref);

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, Resref& r);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, const Resref& r);

} // namespace nw

template <>
struct fmt::formatter<nw::Resref> : fmt::formatter<std::string_view> {
    // parse is inherited from formatter<string_view>.
    template <typename FormatContext>
    auto format(const nw::Resref& r, FormatContext& ctx) const
    {
        return formatter<string_view>::format(r.view(), ctx);
    }
};
