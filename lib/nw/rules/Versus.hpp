#pragma once

#include "attributes.hpp"

namespace nw {

// == Versus ==================================================================
// ============================================================================

struct Versus {
    uint64_t race_flags = 0;
    AlignmentFlags align_flags = AlignmentFlags::none;
    AlignmentAxis align_axis = AlignmentAxis::neither;
    bool trap = false;
};

}
