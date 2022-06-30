#pragma once

#include <absl/hash/hash.h>
#include <absl/strings/string_view.h>

#include <string>

namespace nw {

struct InternedString {
    InternedString() = default;
    explicit InternedString(const std::string* str)
        : string_{str}
    {
    }

    int compare(std::string_view other) const noexcept { return view().compare(other); }
    std::string_view view() const noexcept { return string_ ? *string_ : std::string_view{}; }
    operator bool() const noexcept { return !!string_; }
    operator std::string_view() const noexcept { return view(); }
    bool operator==(InternedString rhs) const noexcept { return string_ == rhs.string_; }
    bool operator==(std::string_view rhs) const noexcept { return compare(rhs) == 0; }
    bool operator<(std::string_view rhs) const noexcept { return compare(rhs) < 0; }
    bool operator<(const InternedString& rhs) const noexcept { return string_ < rhs.string_; }
    bool operator>(std::string_view rhs) const noexcept { return compare(rhs) > 0; }
    bool operator>(const InternedString& rhs) const noexcept { return string_ > rhs.string_; }

private:
    template <typename H>
    friend H AbslHashValue(H h, const InternedString& res);

    const std::string* string_ = nullptr;
};

// Note: The below is only to support heterogenous lookup.  In an ideal case,
// if you were using an InternedString as a key to hashtable, you'd just
// hash and compare the string pointer.

template <typename H>
H AbslHashValue(H h, const InternedString& str)
{
    return H::combine(std::move(h), str.view());
}

struct InternedStringHash {
    using is_transparent = void;

    size_t operator()(absl::string_view v) const
    {
        return absl::Hash<absl::string_view>{}(v);
    }

    size_t operator()(const InternedString& v) const
    {
        absl::string_view v2{v.view().data(), v.view().size()};
        return absl::Hash<absl::string_view>{}(v2);
    }
};

struct InternedStringEq {
    using is_transparent = void;

    bool operator()(const InternedString& a, absl::string_view b) const
    {
        return std::equal(a.view().begin(), a.view().end(), b.begin(), b.end());
    }
    bool operator()(absl::string_view b, const InternedString& a) const
    {
        return std::equal(a.view().begin(), a.view().end(), b.begin(), b.end());
    }
    bool operator()(const InternedString& a, const InternedString& b) const
    {
        return a.compare(b) == 0;
    }
    bool operator()(absl::string_view b, absl::string_view a) const
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end());
    }
};

} // namespace nw
