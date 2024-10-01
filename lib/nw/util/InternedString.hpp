#pragma once

#include "../config.hpp"

#include <absl/hash/hash.h>
#include <absl/strings/string_view.h>

#include <string>

namespace nw {

struct InternedString {
    InternedString() = default;
    explicit InternedString(const String* str)
        : string_{str}
    {
    }

    bool operator==(const InternedString& rhs) const noexcept = default;
    auto operator<=>(const InternedString& rhs) const noexcept = default;

    StringView view() const noexcept { return string_ ? *string_ : StringView{}; }
    operator bool() const noexcept { return !!string_; }

private:
    template <typename H>
    friend H AbslHashValue(H h, const InternedString& res);

    const String* string_ = nullptr;
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

    size_t operator()(StringView v) const
    {
        return absl::Hash<StringView>{}(v);
    }

    size_t operator()(const InternedString& v) const
    {
        return absl::Hash<StringView>{}(v.view());
    }
};

struct InternedStringEq {
    using is_transparent = void;

    bool operator()(const InternedString& a, StringView b) const
    {
        return a.view() == b;
    }
    bool operator()(StringView b, InternedString& a) const
    {
        return a.view() == b;
    }
    bool operator()(const InternedString& a, const InternedString& b) const
    {
        return a == b;
    }
    bool operator()(StringView b, StringView a)
    {
        return a == b;
    }
};

} // namespace nw
