#pragma once

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/MasterFeat.hpp>
#include <nw/rules/system.hpp>
#include <nw/util/EntityView.hpp>

#include <flecs/flecs.h>

#include <array>
#include <vector>

namespace nwn1 {

/// Counts the number of known feats in the range [start, end]
int count_feats_in_range(flecs::entity ent, nw::Feat start, nw::Feat end);

/// Gets all feats for which requirements are met
/// @note This is not yet very useful until a level up parameter is added.
std::vector<nw::Feat> get_all_available_feats(flecs::entity ent);

/// Gets the highest known successor feat
std::pair<nw::Feat, int> has_feat_successor(flecs::entity ent, nw::Feat feat);

/// Gets the highest known feat in range [start, end]
nw::Feat highest_feat_in_range(flecs::entity ent, nw::Feat start, nw::Feat end);

/// Checks if an entity knows a given feat
bool knows_feat(flecs::entity ent, nw::Feat feat);

/// Resolves master feat bonuses
template <typename T, typename U, typename... Args>
std::array<T, sizeof...(Args)> resolve_master_feats(const nw::EntityView<nw::CreatureStats> ent,
    U type, Args... mfeats)
{
    static_assert(nw::is_rule_type<U>::value, "only rule types allowed");
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "result type can only be int or float");

    std::array<T, sizeof...(Args)> result{};
    std::array<nw::MasterFeat, sizeof...(Args)> mfs{mfeats...};
    std::sort(std::begin(mfs), std::end(mfs));

    auto mfreg = nw::kernel::world().get<nw::MasterFeatRegistry>();
    auto stats = ent.get<nw::CreatureStats>();
    if (!mfreg || !stats) {
        return result;
    }

    auto it = std::begin(mfreg->entries());
    size_t i = 0;
    for (auto mf : mfs) {
        nw::MasterFeatEntry mfe{mf, static_cast<int32_t>(type), nw::Feat::invalid};
        const auto& mf_bonus = mfreg->get_bonus(mf);
        if (mf_bonus.empty()) continue;

        it = std::lower_bound(it, std::end(mfreg->entries()), mfe);
        if (it == std::end(mfreg->entries())) {
            break;
        }

        while (it != std::end(mfreg->entries()) && it->type == static_cast<int32_t>(type)) {
            if (stats->has_feat(it->feat)) {
                if (mf_bonus.template is<T>()) {
                    result[i] = mf_bonus.template as<T>();
                } else if (mf_bonus.template is<nw::ModifierFunction>()) {
                    auto res = mf_bonus.template as<nw::ModifierFunction>()(ent.ent);
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

} // namespace nwn1
