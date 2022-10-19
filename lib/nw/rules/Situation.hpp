#pragma once

#include "../util/enum_flags.hpp"
#include "rule_type.hpp"

#include <limits>

namespace nw {

DECLARE_RULE_TYPE(Situation);

using SituationFlag = RuleFlag<Situation, 64>;

struct SituationInfo {
};

} // namespace nw
