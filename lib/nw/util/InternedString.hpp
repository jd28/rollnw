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

    bool operator==(const InternedString& rhs) const noexcept = default;
    auto operator<=>(const InternedString& rhs) const noexcept = default;

    std::string_view view() const noexcept { return string_ ? *string_ : std::string_view{}; }
    operator bool() const noexcept { return !!string_; }

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

    size_t operator()(std::string_view v) const
    {
        return absl::Hash<std::string_view>{}(v);
    }

    size_t operator()(const InternedString& v) const
    {
        return absl::Hash<std::string_view>{}(v.view());
    }
};

struct InternedStringEq {
    using is_transparent = void;

    bool operator()(const InternedString& a, std::string_view b) const
    {
        return a.view() == b;
    }
    bool operator()(std::string_view b, const InternedString& a) const
    {
        return a.view() == b;
    }
    bool operator()(const InternedString& a, const InternedString& b) const
    {
        return a == b;
    }
    bool operator()(std::string_view b, std::string_view a) const
    {
        return a == b;
    }
};

} // namespace nw
