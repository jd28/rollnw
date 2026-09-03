#pragma once

#include "rule_type.hpp"
#include "system.hpp"

#include <algorithm>
#include <cstdint>
#include <tuple>

namespace nw {

// -- Feats -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Feat);

// Not Implemented Yet
// - MINATTACKBONUS
// - MINSPELLLVL
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
    MasterFeatRegistry(MemoryResource* allocator = nw::kernel::global_allocator());

    template <typename T>
    void add(T type, MasterFeat mfeat, Feat feat);
    void clear() noexcept;
    const Vector<MasterFeatEntry>& entries() const noexcept { return entries_; }
    const ModifierVariant& get_bonus(MasterFeat mfeat) const;

    template <typename T>
    void remove(T type, MasterFeat mfeat);

    void set_bonus(MasterFeat mfeat, ModifierVariant bonus);

private:
    Vector<MasterFeatEntry> entries_;
    Vector<ModifierVariant> bonuses_;
    /// Retained from construction; the bonus tables are not yet allocator-aware.
    [[maybe_unused]] MemoryResource* allocator_ = nullptr;
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
