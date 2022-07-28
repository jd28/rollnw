#pragma once

#include "../resources/Resource.hpp"
#include "system.hpp"
#include "type_traits.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace nw {

struct TwoDARowView;

enum struct Feat : int32_t {
    invalid = -1,
};
constexpr Feat make_feat(int32_t index) { return static_cast<Feat>(index); }

template <>
struct is_rule_type_base<Feat> : std::true_type {
};

/// Feat definition
struct FeatInfo {
    FeatInfo() = default;
    FeatInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    bool all_can_use = false;
    int category = -1;
    int max_cr = 0;
    int spell = -1;
    Feat successor = Feat::invalid;
    float cr_value = 0.0f;
    int uses = 0;
    int master = 0;
    bool target_self = false;
    InternedString constant;
    int tools_categories = 0;
    bool hostile = false;
    bool epic = false;
    bool requires_action = false;

    Requirement requirements;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Feat Singleton Component
struct FeatArray {
    using map_type = absl::flat_hash_map<
        InternedString,
        Feat,
        InternedStringHash,
        InternedStringEq>;

    const FeatInfo* get(Feat feat) const noexcept;
    bool is_valid(Feat feat) const noexcept;
    Feat from_constant(std::string_view constant) const;

    std::vector<FeatInfo> entries;
    map_type constant_to_index;
};

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
