#include "Alignment.hpp"

namespace nw {

AlignmentAxis alignment_axis_from_flags(AlignmentFlags flags)
{
    static constexpr AlignmentFlags good_evil{AlignmentFlags::good | AlignmentFlags::evil};
    static constexpr AlignmentFlags law_chaos{AlignmentFlags::lawful | AlignmentFlags::chaotic};

    AlignmentAxis result = AlignmentAxis::neither;

    if (to_bool(flags | good_evil)) {
        result |= AlignmentAxis::good_evil;
    }

    if (to_bool(flags | law_chaos)) {
        result |= AlignmentAxis::law_chaos;
    }

    return result;
}

} // namespace nw
