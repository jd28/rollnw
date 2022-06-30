#pragma once

#include "Feat.hpp"
#include "system.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace nw {

/// Opaque type for Master Feats
enum struct MasterFeat : int32_t {
    invalid = -1,
};

/// Converts integer to ``MasterFeat``
constexpr MasterFeat make_master_feat(int32_t id) { return static_cast<MasterFeat>(id); }

/// @cond NEVER
template <>
struct is_rule_type_base<MasterFeat> {
};
/// @endcond

/// Entry in Master Feat registry
struct MasterFeatEntry {
    MasterFeat mfeat = MasterFeat::invalid;
    int32_t type = -1;
    Feat feat = Feat::invalid;
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
    MasterFeatEntry mfe{mfeat, static_cast<int32_t>(type), feat};

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
                           return entry.mfeat == mfeat && entry.type == static_cast<int32_t>(type);
                       }),
        std::end(entries_));
}

} // namespace nw
