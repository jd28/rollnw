#pragma once

#include "attributes.hpp"

#include <compare>

namespace nw {

// == Versus ==================================================================
// ============================================================================

struct Versus {
    Race race = Race::invalid();
    AlignmentFlags align_flags = AlignmentFlags::none;
    AlignmentAxis align_axis = AlignmentAxis::neither;
    bool trap = false;

    bool operator==(const Versus& rhs) const = default;
    auto operator<=>(const Versus& rhs) const = default;

    operator bool() const noexcept
    {
        return *this != Versus{};
    }

    bool match(const Versus& rhs) const noexcept
    {
        return (race == Race::invalid() || race == rhs.race)
            && (align_flags == AlignmentFlags::none || to_bool(align_flags & rhs.align_flags))
            && (align_axis == AlignmentAxis::neither || align_axis == rhs.align_axis);
    }
};

}
