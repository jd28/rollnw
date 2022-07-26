#include "funcs_feat.hpp"

#include <nw/kernel/Kernel.hpp>

namespace nwn1 {

int count_feats_in_range(flecs::entity ent, nw::Feat start, nw::Feat end)
{
    auto stats = ent.get<nw::CreatureStats>();
    auto result = 0;
    uint32_t s = static_cast<uint32_t>(start), e = static_cast<uint32_t>(end);

    if (!stats) {
        return result;
    }
    while (e >= s) {
        if (stats->has_feat(nw::make_feat(e))) {
            ++result;
        }
        --e;
    }
    return result;
}

std::vector<nw::Feat> get_all_available_feats(flecs::entity ent)
{
    std::vector<nw::Feat> result;
    auto feats = nw::kernel::world().get<nw::FeatArray>();
    auto stats = ent.get<nw::CreatureStats>();

    if (!feats || !stats) {
        return result;
    }

    for (size_t i = 0; i < feats->entries.size(); ++i) {
        if (feats->entries[i].valid()
            && !stats->has_feat(nw::make_feat(uint32_t(i))) // [TODO] Not efficient
            && nw::kernel::rules().meets_requirement(feats->entries[i].requirements, ent)) {
            result.push_back(nw::make_feat(uint32_t(i)));
        }
    }

    return result;
}

bool knows_feat(flecs::entity ent, nw::Feat feat)
{
    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) {
        return false;
    }

    auto it = std::lower_bound(std::begin(stats->feats()), std::end(stats->feats()), feat);
    return it != std::end(stats->feats()) && *it == feat;
}

std::pair<nw::Feat, int> has_feat_successor(flecs::entity ent, nw::Feat feat)
{
    nw::Feat highest = nw::Feat::invalid;
    int count = 0;

    auto featarray = nw::kernel::world().get<nw::FeatArray>();
    if (!featarray) {
        return {highest, count};
    }

    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) {
        return {highest, count};
    }

    auto it = std::lower_bound(std::begin(stats->feats()), std::end(stats->feats()), feat);
    do {
        if (it == std::end(stats->feats()) || *it != feat) {
            break;
        }
        highest = feat;
        ++count;
        const auto next_entry = featarray->get(feat);
        if (!next_entry || next_entry->successor == nw::Feat::invalid) {
            break;
        }
        feat = next_entry->successor;
        it = std::lower_bound(it, std::end(stats->feats()), feat);
    } while (true);

    return {highest, count};
}

nw::Feat highest_feat_in_range(flecs::entity ent, nw::Feat start, nw::Feat end)
{
    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) {
        return nw::Feat::invalid;
    }
    uint32_t s = static_cast<uint32_t>(start), e = static_cast<uint32_t>(end);
    while (e >= s) {
        if (stats->has_feat(nw::make_feat(e))) {
            return nw::make_feat(e);
        }
        --e;
    }
    return nw::Feat::invalid;
}

} // namespace nwn1
