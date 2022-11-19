#include "LevelHistory.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool LevelUp::from_gff(const GffStruct& archive)
{
    uint8_t temp = 0;
    uint16_t word = 0;

    archive.get_to("EpicLevel", epic);

    if (archive.get_to("LvlStatAbility", temp)) {
        ability = nw::Ability::make(temp);
    }

    if (archive.get_to("LvlStatClass", temp)) {
        class_ = nw::Class::make(temp);
    }

    if (archive.get_to("LvlStatHitDie", temp)) {
        hitpoints = temp;
    }

    if (archive.get_to("SkillPoints", word)) {
        skillpoints = word;
    }

    auto feat_list = archive["FeatList"];
    feats.reserve(feat_list.size());
    for (size_t i = 0; i < feat_list.size(); ++i) {
        if (feat_list[i].get_to("Feat", word)) {
            feats.push_back(nw::Feat::make(word));
        }
    }

    auto skill_list = archive["SkillList"];
    skills.reserve(skill_list.size());
    for (size_t i = 0; i < skill_list.size(); ++i) {
        if (skill_list[i].get_to("Rank", temp)) {
            skills.push_back(temp);
        }
    }

    return true;
}

bool LevelUp::to_gff(GffBuilderStruct& archive) const
{
    archive.add_field("EpicLevel", uint8_t(epic));
    if (ability != Ability::invalid()) {
        archive.add_field("LvlStatAbility", uint8_t(*ability));
    }
    archive.add_field("LvlStatClass", uint8_t(*class_));
    archive.add_field("LvlStatHitDie", uint8_t(hitpoints));
    archive.add_field("SkillPoints", uint16_t(skillpoints));

    auto feat_list = archive.add_list("FeatList");
    for (auto it : feats) {
        feat_list.push_back(0).add_field("Feat", uint16_t(*it));
    }

    auto skill_list = archive.add_list("SkillList");
    for (auto it : skills) {
        skill_list.push_back(0).add_field("Rank", uint8_t(it));
    }
    return true;
}

void from_json(const nlohmann::json& json, LevelUp& entry)
{

    entry.class_ = nw::Class::make(json.at("class").get<int32_t>());
    entry.ability = nw::Ability::make(json.at("ability").get<int32_t>());

    json.at("epic").get_to(entry.epic);
    json.at("hitpoints").get_to(entry.hitpoints);
    json.at("skillpoints").get_to(entry.skillpoints);

    const auto& feats = json.at("feats");
    for (auto feat : feats) {
        entry.feats.push_back(nw::Feat::make(feat.get<int32_t>()));
    }

    json.at("skills").get_to(entry.skills);
}

void to_json(nlohmann::json& json, const LevelUp& entry)
{
    json["class"] = *entry.class_;
    json["ability"] = entry.ability;
    json["epic"] = entry.epic;
    json["hitpoints"] = entry.hitpoints;
    json["skillpoints"] = entry.skillpoints;

    json["feats"] = nlohmann::json::array();
    for (auto feat : entry.feats) {
        json["feats"].push_back(*feat);
    }

    json["skills"] = entry.skills;
}

} // namespace nw
