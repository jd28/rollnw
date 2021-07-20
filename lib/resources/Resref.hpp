#pragma once

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
    /// @name Types
    /// @{
    static constexpr size_t max_size = 16;
    using Storage = std::array<char, max_size>;
    using value_type = typename Storage::value_type;
    using size_type = typename Storage::size_type;
    /// @}

    /// @name Constructors
    /// @{
    Resref() = default;
    Resref(Storage data) noexcept;
    Resref(const char* string) noexcept;
    Resref(std::string_view string) noexcept;
    /// @}

    /// @name Functions
    /// @{

    /**
     * @brief Checks if the underlying array has no non-null characters
     */
    bool empty() const noexcept;

    /**
     * @brief Returns the number of char elements in the array, excluding nulls.
     */
    size_type length() const noexcept;

    /**
     * @brief Creates ``std::string`` of underlying array
     */
    std::string string() const;

    /**
     * @brief Creates ``std::string_view`` of underlying array
     *
     * @note The resulting ``std::string_view`` does not contain null padding.
     */
    std::string_view view() const noexcept;
    /// @}

private:
    Storage data_;
};

// [TODO] Replace with spaceship operator when c++20 is better supported.

/**
 * @brief Overloads ``operator==`` for ``nw::Resref``
 */
bool operator==(const Resref& lhs, const Resref& rhs) noexcept;

/**
 * @brief Overloads ``operator!=`` for ``nw::Resref``
 */
bool operator!=(const Resref& lhs, const Resref& rhs) noexcept;

/**
 * @brief Overloads ``operator<`` for ``nw::Resref``
 */
bool operator<(const Resref& lhs, const Resref& rhs) noexcept;

/**
 * @brief Overloads ``operator<<`` for ``nw::Resref``
 *
 * @param out Any output stream
 * @param resref A ``nw::Resref``
 * @return std::ostream& ``out`` parameter
 */
std::ostream& operator<<(std::ostream& out, const Resref& resref);

} // namespace nw
