#pragma once

#include "../resources/Resource.hpp"
#include "../util/InternedString.hpp"
#include "system.hpp"

#include <flecs/flecs.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace nw {

/// Feat definition
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
    nw::Index index;
    int tools_categories = 0;
    bool hostile = false;
    bool epic = false;
    bool requires_action = false;

    Requirement requirements;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Feat Singleton Component
struct FeatArray {
    std::vector<Feat> entries;
};

std::vector<size_t> get_all_available_feats(flecs::entity ent);

// Not Implemented Yet
// - MINATTACKBONUS
// - MINSPELLLVL
// - PREREQFEAT1, PREREQFEAT2
// - MinLevelClass
// - MinFortSave
// - OrReqFeat0, OrReqFeat1, OrReqFeat2, OrReqFeat3, OrReqFeat4
// - REQSKILL, ReqSkillMinRanks
// - REQSKILL2, ReqSkillMinRanks2

// Unimplemented - Obsolete
// GAINMULTIPLE
// EFFECTSSTACK

} // namespace nw
