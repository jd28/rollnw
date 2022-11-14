#include "CreatureStats.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

bool CreatureStats::from_gff(const GffStruct& archive)
{
    archive.get_to("Str", abilities_[0]);
    archive.get_to("Dex", abilities_[1]);
    archive.get_to("Con", abilities_[2]);
    archive.get_to("Int", abilities_[3]);
    archive.get_to("Wis", abilities_[4]);
    archive.get_to("Cha", abilities_[5]);

    auto skill_list = archive["SkillList"];
    skills_.resize(skill_list.size(), 0);
    for (size_t i = 0; i < skill_list.size(); ++i) {
        skill_list[i].get_to("Rank", skills_[i]);
    }

    auto feat_list = archive["FeatList"];
    size_t sz = feat_list.size();
    feats_.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        uint16_t temp;
        if (feat_list[i].get_to("Feat", temp)) {
            feats_[i] = Feat::make(temp);
        }
    }

    std::sort(std::begin(feats_), std::end(feats_));

    archive.get_to("fortbonus", save_bonus.fort);
    archive.get_to("refbonus", save_bonus.reflex);
    archive.get_to("willbonus", save_bonus.will);

    return false;
}

bool CreatureStats::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("abilities").get_to(abilities_);
        archive.at("feats").get_to(feats_);
        archive.at("skills").get_to(skills_);
        archive.at("save_bonus").get_to(save_bonus);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }

    std::sort(std::begin(feats_), std::end(feats_));

    return true;
}

bool CreatureStats::to_gff(GffBuilderStruct& archive) const
{
    archive.add_field("Str", abilities_[0])
        .add_field("Dex", abilities_[1])
        .add_field("Con", abilities_[2])
        .add_field("Int", abilities_[3])
        .add_field("Wis", abilities_[4])
        .add_field("Cha", abilities_[5])
        .add_field("fortbonus", save_bonus.fort)
        .add_field("refbonus", save_bonus.reflex)
        .add_field("willbonus", save_bonus.will);

    auto& ft_list = archive.add_list("FeatList");
    for (auto ft : feats_) {
        ft_list.push_back(1).add_field("Feat", static_cast<uint16_t>(*ft));
    }

    auto& sk_list = archive.add_list("SkillList");
    for (uint8_t sk : skills_) {
        sk_list.push_back(0).add_field("Rank", sk);
    }

    return true;
}

nlohmann::json CreatureStats::to_json() const
{
    nlohmann::json j;
    j["abilities"] = abilities_;
    j["feats"] = feats_;
    j["skills"] = skills_;
    j["save_bonus"] = save_bonus;
    return j;
}

bool CreatureStats::add_feat(Feat feat)
{
    auto it = std::lower_bound(std::begin(feats_), std::end(feats_), feat);
    if (it == std::end(feats_)) {
        feats_.push_back(feat);
    } else if (*it != feat) {
        feats_.insert(it, feat);
        return true;
    }
    return false;
}

const std::vector<Feat>& CreatureStats::feats() const noexcept
{
    return feats_;
}

int CreatureStats::get_ability_score(Ability id) const
{
    if (id.idx() > abilities_.size()) {
        return 0;
    }
    return abilities_[id.idx()];
}

int CreatureStats::get_skill_rank(Skill id) const
{
    if (id.idx() > skills_.size()) {
        return 0;
    }
    return skills_[id.idx()];
}

bool CreatureStats::has_feat(Feat feat) const noexcept
{
    auto it = std::lower_bound(std::begin(feats_), std::end(feats_), feat);
    return it != std::end(feats_) && *it == feat;
}

bool CreatureStats::set_ability_score(Ability id, int value)
{
    if (id.idx() > abilities_.size()) {
        return 0;
    }
    return (abilities_[id.idx()] = uint8_t(value));
}

bool CreatureStats::set_skill_rank(Skill id, int value)
{
    if (id.idx() > skills_.size()) {
        return 0;
    }
    return (skills_[id.idx()] = uint8_t(value));
}

} // namespace nw
