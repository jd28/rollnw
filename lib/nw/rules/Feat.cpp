#include "Feat.hpp"

#include "../kernel/Kernel.hpp"
#include "../objects/components/CreatureStats.hpp"

namespace nw {

std::vector<size_t> get_all_available_feats(flecs::entity ent)
{
    std::vector<size_t> result;
    auto feats = kernel::world().get<FeatArray>();
    auto stats = ent.get<CreatureStats>();

    if (!feats || !stats) { return result; }

    for (size_t i = 0; i < feats->entries.size(); ++i) {
        if (feats->entries[i].valid()
            && !stats->has_feat(i) // [TODO] Not efficient
            && kernel::rules().meets_requirement(feats->entries[i].requirements, ent)) {
            result.push_back(i);
        }
    }

    return result;
}

} // namespace nw
