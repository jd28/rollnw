#include "funcs_feat.hpp"

#include <nw/kernel/Rules.hpp>

namespace nwn1 {

int count_feats_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end)
{
    if (!obj) { return 0; }

    int result = 0;
    int32_t s = *start, e = *end;

    while (e >= s) {
        if (obj->stats.has_feat(nw::Feat::make(e))) {
            ++result;
        }
        --e;
    }
    return result;
}

std::vector<nw::Feat> get_all_available_feats(const nw::Creature* obj)
{
    if (!obj) return {};

    std::vector<nw::Feat> result;

    auto& feats = nw::kernel::rules().feats;
    if (!obj) {
        return result;
    }

    for (size_t i = 0; i < feats.entries.size(); ++i) {
        if (feats.entries[i].valid()
            && !obj->stats.has_feat(nw::Feat::make(uint32_t(i))) // [TODO] Not efficient
            && nw::kernel::rules().meets_requirement(feats.entries[i].requirements, obj)) {
            result.push_back(nw::Feat::make(uint32_t(i)));
        }
    }

    return result;
}

bool knows_feat(const nw::Creature* obj, nw::Feat feat)
{
    if (!obj) { return false; }

    auto it = std::lower_bound(std::begin(obj->stats.feats()), std::end(obj->stats.feats()), feat);
    return it != std::end(obj->stats.feats()) && *it == feat;
}

std::pair<nw::Feat, int> has_feat_successor(const nw::Creature* obj, nw::Feat feat)
{
    nw::Feat highest = nw::Feat::invalid();
    int count = 0;

    auto featarray = &nw::kernel::rules().feats;
    if (!featarray || !obj) {
        return {highest, count};
    }

    auto it = std::lower_bound(std::begin(obj->stats.feats()), std::end(obj->stats.feats()), feat);
    do {
        if (it == std::end(obj->stats.feats()) || *it != feat) {
            break;
        }
        highest = feat;
        ++count;
        const auto next_entry = featarray->get(feat);
        if (!next_entry || next_entry->successor == nw::Feat::invalid()) {
            break;
        }
        feat = next_entry->successor;
        it = std::lower_bound(it, std::end(obj->stats.feats()), feat);
    } while (true);

    return {highest, count};
}

nw::Feat highest_feat_in_range(const nw::Creature* obj, nw::Feat start, nw::Feat end)
{
    if (!obj) { return nw::Feat::invalid(); }

    int32_t s = *start, e = *end;
    while (e >= s) {
        if (obj->stats.has_feat(nw::Feat::make(e))) {
            return nw::Feat::make(e);
        }
        --e;
    }
    return nw::Feat::invalid();
}

} // namespace nwn1
