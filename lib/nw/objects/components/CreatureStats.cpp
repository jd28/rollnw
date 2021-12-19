#include "CreatureStats.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

bool CreatureStats::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("Str", abilities[0]);
    archive.get_to("Dex", abilities[1]);
    archive.get_to("Con", abilities[2]);
    archive.get_to("Int", abilities[3]);
    archive.get_to("Wis", abilities[4]);
    archive.get_to("Cha", abilities[5]);

    auto skill_list = archive["SkillList"];
    skills.resize(skill_list.size(), 0);
    for (size_t i = 0; i < skill_list.size(); ++i) {
        skill_list[i].get_to("Rank", skills[i]);
    }

    auto feat_list = archive["FeatList"];
    size_t sz = feat_list.size();
    feats.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        feat_list[i].get_to("Feat", feats[i]);
    }

    std::sort(std::begin(feats), std::end(feats));

    archive.get_to("fortbonus", save_bonus.fort);
    archive.get_to("refbonus", save_bonus.reflex);
    archive.get_to("willbonus", save_bonus.will);

    return false;
}

bool CreatureStats::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("abilities").get_to(abilities);
        archive.at("feats").get_to(feats);
        archive.at("skills").get_to(skills);
        archive.at("save_bonus").get_to(save_bonus);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return false;
}

nlohmann::json CreatureStats::to_json() const
{
    nlohmann::json j;
    j["abilities"] = abilities;
    j["feats"] = feats;
    j["skills"] = skills;
    j["save_bonus"] = save_bonus;
    return j;
}

} // namespace nw
