#include "CreatureStats.hpp"

#include <algorithm>

namespace nw {

CreatureStats::CreatureStats(const GffStruct gff)
{
    gff.get_to("Str", abilities[0]);
    gff.get_to("Dex", abilities[1]);
    gff.get_to("Con", abilities[2]);
    gff.get_to("Int", abilities[3]);
    gff.get_to("Wis", abilities[4]);
    gff.get_to("Cha", abilities[5]);

    auto skill_list = gff["SkillList"];
    skills.resize(skill_list.size(), 0);
    for (size_t i = 0; i < skill_list.size(); ++i) {
        skill_list[i].get_to("Rank", skills[i]);
    }

    auto feat_list = gff["FeatList"];
    size_t sz = feat_list.size();
    feats.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        feat_list[i].get_to("Feat", feats[i]);
    }

    std::sort(std::begin(feats), std::end(feats));

    gff.get_to("fortbonus", save_bonus.fort);
    gff.get_to("refbonus", save_bonus.reflex);
    gff.get_to("willbonus", save_bonus.will);
}

} // namespace nw
