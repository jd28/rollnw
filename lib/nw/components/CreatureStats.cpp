#include "CreatureStats.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

bool CreatureStats::from_gff(const GffStruct& archive)
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
        archive.at("abilities").get_to(abilities);
        archive.at("feats").get_to(feats_);
        archive.at("skills").get_to(skills);
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
    archive.add_field("Str", abilities[0])
        .add_field("Dex", abilities[1])
        .add_field("Con", abilities[2])
        .add_field("Int", abilities[3])
        .add_field("Wis", abilities[4])
        .add_field("Cha", abilities[5])
        .add_field("fortbonus", save_bonus.fort)
        .add_field("refbonus", save_bonus.reflex)
        .add_field("willbonus", save_bonus.will);

    auto& ft_list = archive.add_list("FeatList");
    for (auto ft : feats_) {
        ft_list.push_back(1).add_field("Feat", static_cast<uint16_t>(ft));
    }

    auto& sk_list = archive.add_list("SkillList");
    for (uint8_t sk : skills) {
        sk_list.push_back(0).add_field("Rank", sk);
    }

    return true;
}

nlohmann::json CreatureStats::to_json() const
{
    nlohmann::json j;
    j["abilities"] = abilities;
    j["feats"] = feats_;
    j["skills"] = skills;
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

bool CreatureStats::has_feat(Feat feat) const noexcept
{
    auto it = std::lower_bound(std::begin(feats_), std::end(feats_), feat);
    return it != std::end(feats_) && *it == feat;
}

} // namespace nw
