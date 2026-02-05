#pragma once

#include <absl/hash/hash.h>

#include <compare>
#include <cstdint>

namespace nw {

template <typename Tag, typename Underlying = int32_t>
struct StrongID {
    using underlying_type = Underlying;

    underlying_type value{};

    // Constructors
    constexpr StrongID() = default;
    constexpr explicit StrongID(underlying_type v)
        : value(v)
    {
    }

    constexpr underlying_type get() const { return value; }

    constexpr auto operator<=>(const StrongID&) const = default;
    explicit operator underlying_type() const { return value; }
    uint64_t to_u64() const noexcept { return static_cast<uint64_t>(value); }
};

template <typename H, typename Tag>
H AbslHashValue(H h, const StrongID<Tag>& id)
{
    return H::combine(std::move(h), id.value);
}

template <typename Tag>
struct StrongIDHash {
    using is_transparent = void;

    size_t operator()(const StrongID<Tag>& v) const
    {
        return absl::Hash<typename StrongID<Tag>::underlying_type>{}(v.value);
    }
};

template <typename Tag>
struct StrongIDEq {
    using is_transparent = void;
    bool operator()(const StrongID<Tag> a, const StrongID<Tag> b) const
    {
        return a == b;
    }
};

}
