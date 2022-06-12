#pragma once

#include "../kernel/Strings.hpp"
#include "../util/Variant.hpp"

namespace nw {

// == Index ===================================================================

/**
 * @brief An `Index` is an InternedString and an `size_t`.
 *
 * The goal here is to abstract over 2da row entries, reading from the "Constant"
 * columns and other related things.  This can really only half work with NWN(:EE):
 *
 * - not all 2da's have Constant columns
 * - the above are for readers and aren't mandatory, nor necessarily unique.
 *
 * ``nw::Index`` is implicitly convertible to ``size_t`` so easily usable as an
 * index into an array, vector, etc.
 */
struct Index {
    Index() = default;
    Index(InternedString name_, size_t value_)
        : name{name_}
        , value{value_}
    {
    }

    InternedString name;
    size_t value;

    bool empty() const noexcept { return !name; }

    size_t operator*() const noexcept { return value; }

    operator size_t() const noexcept { return value; }

    bool operator==(const Index& rhs)
    {
        return std::tie(name, value) == std::tie(rhs.name, rhs.value);
    }

    bool operator<(const Index& rhs)
    {
        return std::tie(name, value) < std::tie(rhs.name, rhs.value);
    }
};

} // namespace nw
