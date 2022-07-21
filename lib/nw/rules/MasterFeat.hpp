#pragma once

#include "../components/CreatureStats.hpp"
#include "../util/EntityView.hpp"
#include "Feat.hpp"
#include "Modifier.hpp"
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

    template <typename T, typename U, typename... Args>
    std::array<T, sizeof...(Args)> resolve(const nw::EntityView<nw::CreatureStats> ent,
        U type, Args... mfeats) const;

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

/// Resolves master feat bonuses
template <typename T, typename U, typename... Args>
std::array<T, sizeof...(Args)> MasterFeatRegistry::resolve(const EntityView<CreatureStats> ent,
    U type, Args... mfeats) const
{
    static_assert(is_rule_type<U>::value, "only rule types allowed");
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "result type can only be int or float");

    std::array<T, sizeof...(Args)> result{};
    std::array<MasterFeat, sizeof...(Args)> mfs{mfeats...};
    std::sort(std::begin(mfs), std::end(mfs));

    auto stats = ent.get<CreatureStats>();
    if (!stats) {
        return result;
    }

    auto it = std::begin(entries());
    size_t i = 0;
    for (auto mf : mfs) {
        MasterFeatEntry mfe{mf, static_cast<int32_t>(type), Feat::invalid};
        const auto& mf_bonus = get_bonus(mf);
        if (mf_bonus.empty()) continue;

        it = std::lower_bound(it, std::end(entries()), mfe);
        if (it == std::end(entries())) {
            break;
        }

        while (it != std::end(entries()) && it->type == static_cast<int32_t>(type)) {
            if (stats->has_feat(it->feat)) {
                if (mf_bonus.template is<T>()) {
                    result[i] = mf_bonus.template as<T>();
                } else if (mf_bonus.template is<ModifierFunction>()) {
                    auto res = mf_bonus.template as<ModifierFunction>()(ent.ent);
                    if (res.template is<T>()) {
                        result[i] = res.template as<T>();
                    }
                }
                break;
            }
            ++it;
        }
        ++i;
    }
    return result;
}

} // namespace nw
