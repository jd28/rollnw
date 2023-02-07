#include "LevelHistory.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

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

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(LevelUp& self, const GffStruct& archive)
{
    uint8_t temp = 0;
    uint16_t word = 0;

    archive.get_to("EpicLevel", self.epic);

    if (archive.get_to("LvlStatAbility", temp, false)) {
        self.ability = nw::Ability::make(temp);
    }

    if (archive.get_to("LvlStatClass", temp)) {
        self.class_ = nw::Class::make(temp);
    }

    if (archive.get_to("LvlStatHitDie", temp)) {
        self.hitpoints = temp;
    }

    if (archive.get_to("SkillPoints", word)) {
        self.skillpoints = word;
    }

    auto feat_list = archive["FeatList"];
    self.feats.reserve(feat_list.size());
    for (size_t i = 0; i < feat_list.size(); ++i) {
        if (feat_list[i].get_to("Feat", word)) {
            self.feats.push_back(nw::Feat::make(word));
        }
    }

    auto skill_list = archive["SkillList"];
    self.skills.reserve(skill_list.size());
    for (size_t i = 0; i < skill_list.size(); ++i) {
        if (skill_list[i].get_to("Rank", temp)) {
            self.skills.push_back(temp);
        }
    }

    for (size_t i = 0; i < 10; ++i) {
        auto key = fmt::format("KnownList{}", i);
        auto known = archive[key];
        for (size_t j = 0; j < known.size(); ++j) {
            if (known[j].get_to("Spell", word)) {
                self.known_spells.emplace_back(int32_t(i), nw::Spell::make(word));
            }
        }
    }
    std::sort(std::begin(self.known_spells), std::end(self.known_spells));

    return true;
}

bool serialize(const LevelUp& self, GffBuilderStruct& archive)
{
    archive.add_field("EpicLevel", uint8_t(self.epic));
    if (self.ability != Ability::invalid()) {
        archive.add_field("LvlStatAbility", uint8_t(*self.ability));
    }
    archive.add_field("LvlStatClass", uint8_t(*self.class_));
    archive.add_field("LvlStatHitDie", uint8_t(self.hitpoints));
    archive.add_field("SkillPoints", uint16_t(self.skillpoints));

    auto feat_list = archive.add_list("FeatList");
    for (auto it : self.feats) {
        feat_list.push_back(0).add_field("Feat", uint16_t(*it));
    }

    auto skill_list = archive.add_list("SkillList");
    for (auto it : self.skills) {
        skill_list.push_back(0).add_field("Rank", uint8_t(it));
    }

    if (self.known_spells.size()) {
        int prev = self.known_spells[0].first;
        GffBuilderList& known = archive.add_list(fmt::format("KnownList{}", self.known_spells[0].first));
        for (size_t i = 1; i < self.known_spells.size(); ++i) {
            auto [level, spell] = self.known_spells[i];
            if (level != prev) {
                known = archive.add_list(fmt::format("KnownList{}", level));
                prev = level;
            }
            known.push_back(0).add_field("Spell", uint16_t(*spell));
        }
    }

    return true;
}
#endif

} // namespace nw
