#include "LevelHistory.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

bool LevelUp::from_gff(const GffStruct& archive)
{
    uint8_t temp = 0;
    uint16_t word = 0;

    archive.get_to("EpicLevel", epic);

    if (archive.get_to("LvlStatAbility", temp, false)) {
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

    for (size_t i = 0; i < 10; ++i) {
        auto key = fmt::format("KnownList{}", i);
        auto known = archive[key];
        for (size_t j = 0; j < known.size(); ++j) {
            if (known[j].get_to("Spell", word)) {
                known_spells.emplace_back(i, nw::Spell::make(word));
            }
        }
    }
    std::sort(std::begin(known_spells), std::end(known_spells));

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

    if (known_spells.size()) {
        int prev = known_spells[0].first;
        GffBuilderList& known = archive.add_list(fmt::format("KnownList{}", known_spells[0].first));
        for (size_t i = 1; i < known_spells.size(); ++i) {
            auto [level, spell] = known_spells[i];
            if (level != prev) {
                known = archive.add_list(fmt::format("KnownList{}", level));
                prev = level;
            }
            known.push_back(0).add_field("Spell", uint16_t(*spell));
        }
    }

    return true;
}

void from_json(const nlohmann::json& json, LevelUp& entry)
{
    json.at("ability").get_to(entry.ability);
    json.at("class").get_to(entry.class_);
    json.at("epic").get_to(entry.epic);
    json.at("feats").get_to(entry.feats);
    json.at("hitpoints").get_to(entry.hitpoints);
    json.at("known_spells").get_to(entry.known_spells);
    json.at("skillpoints").get_to(entry.skillpoints);
    json.at("skills").get_to(entry.skills);
}

void to_json(nlohmann::json& json, const LevelUp& entry)
{
    json["ability"] = entry.ability;
    json["class"] = *entry.class_;
    json["epic"] = entry.epic;
    json["feats"] = entry.feats;
    json["hitpoints"] = entry.hitpoints;
    json["known_spells"] = entry.known_spells;
    json["skillpoints"] = entry.skillpoints;
    json["skills"] = entry.skills;
}

} // namespace nw
