#pragma once

#include "../resources/Resource.hpp"
#include "system.hpp"

#include <cstdint>
#include <limits>

namespace nw {

struct Feat {
    uint32_t name = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    bool all_can_use = false;
    int category = -1;
    int max_cr = 0;
    int spell = -1;
    size_t successor = std::numeric_limits<size_t>::max();
    float cr_value = 0.0f;
    int uses = 0;
    int master = 0;
    bool target_self = false;
    Constant constant;
    int tools_categories = 0;
    bool hostile = false;
    bool epic = false;
    bool requires_action = false;

    Requirement requirements;

    operator bool() const noexcept { return name != 0xFFFFFFFF; }
};

// Not Implemented Yet
// - MINATTACKBONUS
// - MINSTR, MINDEX, MININT, MINWIS, MINCON, MINCHA
// - MINSPELLLVL
// - PREREQFEAT1, PREREQFEAT2
// - MinLevel
// - MinLevelClass
// - MinFortSave
// - OrReqFeat0, OrReqFeat1, OrReqFeat2, OrReqFeat3, OrReqFeat4
// - REQSKILL, ReqSkillMinRanks
// - REQSKILL2, ReqSkillMinRanks2

// Unimplemented - Obsolete
// GAINMULTIPLE
// EFFECTSSTACK

} // namespace nw
