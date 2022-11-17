#pragma once

#include "../resources/Resource.hpp"
#include "rule_type.hpp"
#include "system.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace nw {

struct TwoDARowView;

// -- Feats -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Feat);

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
    Feat successor = Feat::invalid();
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
using FeatArray = RuleTypeArray<Feat, FeatInfo>;

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

// -- Master Feats ------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(MasterFeat);

/// Entry in Master Feat registry
struct MasterFeatEntry {
    MasterFeat mfeat = MasterFeat::invalid();
    int32_t type = -1;
    Feat feat = Feat::invalid();
};

inline bool operator<(const MasterFeatEntry& lhs, const MasterFeatEntry& rhs)
{
    return std::tie(lhs.mfeat, lhs.type, lhs.feat) < std::tie(rhs.mfeat, rhs.type, rhs.feat);
}

inline bool operator==(const MasterFeatEntry& lhs, const MasterFeatEntry& rhs)
{
    return std::tie(lhs.mfeat, lhs.type, lhs.feat) == std::tie(rhs.mfeat, rhs.type, rhs.feat);
}

struct MasterFeatRegistry {
    template <typename T>
    void add(T type, MasterFeat mfeat, Feat feat);
    void clear() noexcept;
    const std::vector<MasterFeatEntry>& entries() const noexcept { return entries_; }
    const ModifierVariant& get_bonus(MasterFeat mfeat) const;

    template <typename T>
    void remove(T type, MasterFeat mfeat);

    void set_bonus(MasterFeat mfeat, ModifierVariant bonus);

private:
    std::vector<MasterFeatEntry> entries_;
    std::vector<ModifierVariant> bonuses_;
};

template <typename T>
void MasterFeatRegistry::add(T type, MasterFeat mfeat, Feat feat)
{
    static_assert(is_rule_type<T>::value, "only rule types allowed");
    MasterFeatEntry mfe{mfeat, static_cast<int32_t>(*type), feat};

    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), mfe);
    if (it == std::end(entries_)) {
        entries_.push_back(mfe);
    } else if (*it == mfe) {
        return;
    } else {
        entries_.insert(it, mfe);
    }
}

template <typename T>
void MasterFeatRegistry::remove(T type, MasterFeat mfeat)
{
    static_assert(is_rule_type<T>::value, "only rule types allowed");
    entries_.erase(std::remove_if(std::begin(entries_), std::end(entries_),
                       [type, mfeat](const auto& entry) {
                           return entry.mfeat == mfeat && entry.type == *type;
                       }),
        std::end(entries_));
}

} // namespace nw
