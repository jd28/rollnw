#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nw {

/**
 * @brief test
 *
 */
struct LocString {
    /// @name Types
    /// @{
    using LocStringPair = std::pair<uint32_t, std::string>;
    using Storage = std::vector<LocStringPair>;
    using size_type = Storage::size_type;
    /// @}

    /// @name Constructors
    /// @{
    explicit LocString(uint32_t strref = -1u);
    LocString(const LocString&) = default;
    LocString(LocString&&) = default;
    /// @}

    /// @name Functions
    /// @{
    /**
     * @brief Add a localized string.
     */
    void add(uint32_t language, std::string str, bool feminine = false);

    /**
     * @brief Gets a localized string.
     */
    std::string get(uint32_t language, bool feminine = false) const;

    /**
     * @brief Determines if a localized string is set
     */
    bool contains(uint32_t language, bool feminine = false) const;

    /**
     * @brief Gets number of localized strings
     */
    size_type size() const;

    /**
     * @brief Gets string reference
     */
    uint32_t strref() const;
    /// @}

    /// @name Operators
    /// @{
    LocString& operator=(const LocString&) = default;
    LocString& operator=(LocString&&) = default;
    bool operator==(const LocString& other) const;
    /// @}

private:
    uint32_t strref_ = -1u;
    Storage strings_;
};

} // namespace nw
