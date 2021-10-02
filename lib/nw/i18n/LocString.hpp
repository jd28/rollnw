#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace nw {

struct LocString {
    using LocStringPair = std::pair<uint32_t, std::string>;
    using Storage = std::vector<LocStringPair>;
    using size_type = Storage::size_type;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;

    explicit LocString(uint32_t strref = std::numeric_limits<uint32_t>::max());
    LocString(const LocString&) = default;
    LocString(LocString&&) = default;

    /// Add a localized string.
    void add(uint32_t language, std::string str, bool feminine = false);

    /// Gets a localized string.
    std::string get(uint32_t language, bool feminine = false) const;

    /// Determines if a localized string is set
    bool contains(uint32_t language, bool feminine = false) const;

    /// Gets number of localized strings
    size_type size() const;

    /// Gets string reference
    uint32_t strref() const;

    /// Iterators
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;

    /// Operators
    LocString& operator=(const LocString&) = default;
    LocString& operator=(LocString&&) = default;
    bool operator==(const LocString& other) const;

private:
    uint32_t strref_ = std::numeric_limits<uint32_t>::max();
    Storage strings_;
};

} // namespace nw
