#pragma once

#include "../util/enum_flags.hpp"

namespace nw {

enum struct AlignmentAxis {
    neither = 0x0,
    law_chaos = 0x1,
    good_evil = 0x2,
    both = 0x3,
};

DEFINE_ENUM_FLAGS(AlignmentAxis)

enum struct AlignmentType {
    all = 0,
    neutral = 1,
    lawful = 2,
    chaotic = 3,
    good = 4,
    evil = 5,
};

enum struct AlignmentFlags {
    none = 0x0,
    neutral = 0x01,
    lawful = 0x02,
    chaotic = 0x04,
    good = 0x08,
    evil = 0x10,
};

/// Gets alignment axis from alignment flags
AlignmentAxis alignment_axis_from_flags(AlignmentFlags flags);

DEFINE_ENUM_FLAGS(AlignmentFlags)

} // namespace nw
