#include "funcs_feat.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/Creature.hpp"
#include "../../rules/Feat.hpp"

namespace nwn1 {

int count_feats_in_range(flecs::entity ent, nw::Index start, nw::Index end)
{
    auto stats = ent.get<nw::CreatureStats>();
    auto result = 0;
    size_t s = start, e = end;
    if (!stats) { return result; }
    while (e >= s) {
        if (stats->has_feat(e)) { ++result; }
        --e;
    }
    return result;
}

std::vector<size_t> get_all_available_feats(flecs::entity ent)
{
    std::vector<size_t> result;
    auto feats = nw::kernel::world().get<nw::FeatArray>();
    auto stats = ent.get<nw::CreatureStats>();

    if (!feats || !stats) { return result; }

    for (size_t i = 0; i < feats->entries.size(); ++i) {
        if (feats->entries[i].valid()
            && !stats->has_feat(i) // [TODO] Not efficient
            && nw::kernel::rules().meets_requirement(feats->entries[i].requirements, ent)) {
            result.push_back(i);
        }
    }

    return result;
}

bool knows_feat(flecs::entity ent, nw::Index feat)
{
    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) { return false; }

    auto it = std::lower_bound(std::begin(stats->feats()), std::end(stats->feats()), feat);
    return it != std::end(stats->feats()) && *it == feat;
}

std::pair<nw::Index, int> has_feat_successor(flecs::entity ent, nw::Index feat)
{
    nw::Index highest;
    int count = 0;

    auto featarray = nw::kernel::world().get<nw::FeatArray>();
    if (!featarray) { return {highest, count}; }

    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) { return {highest, count}; }

    auto it = std::lower_bound(std::begin(stats->feats()), std::end(stats->feats()), feat);
    do {
        if (it == std::end(stats->feats()) || *it != feat) { break; }
        highest = feat;
        ++count;
        const auto& next_entry = featarray->entries[feat];
        if (next_entry.successor == std::numeric_limits<size_t>::max()) {
            break;
        }
        feat = featarray->entries[next_entry.successor].index;
        it = std::lower_bound(it, std::end(stats->feats()), feat);
    } while (true);

    return {highest, count};
}

size_t highest_feat_in_range(flecs::entity ent, nw::Index start, nw::Index end)
{
    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) { return ~0u; }
    size_t s = start, e = end;
    while (e >= s) {
        if (stats->has_feat(e)) { return e; }
        --e;
    }
    return ~0u;
}

} // namespace nwn1
