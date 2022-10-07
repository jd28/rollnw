#include "CombatInfo.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool CombatInfo::from_gff(const GffStruct& archive)
{
    archive.get_to("NaturalAC", ac_natural);

    size_t sz = archive["SpecAbilityList"].size();
    special_abilities.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        SpecialAbility sa;
        archive["SpecAbilityList"][i].get_to("Spell", sa.spell);
        archive["SpecAbilityList"][i].get_to("SpellCasterLevel", sa.level);
        archive["SpecAbilityList"][i].get_to("SpellFlags", sa.flags);
        special_abilities.push_back(sa);
    }
    return true;
}

bool CombatInfo::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("ac_natural").get_to(ac_natural);

        auto& arr = archive.at("special_abilities");
        special_abilities.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            arr[i].at("spell").get_to(special_abilities[i].spell);
            arr[i].at("level").get_to(special_abilities[i].level);
            arr[i].at("flags").get_to(special_abilities[i].flags);
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool CombatInfo::to_gff(GffBuilderStruct& archive) const
{
    archive.add_field("NaturalAC", ac_natural);
    auto& list = archive.add_list("SpecAbilityList");
    for (const auto& spec : special_abilities) {
        list.push_back(4)
            .add_field("Spell", spec.spell)
            .add_field("SpellCasterLevel", spec.level)
            .add_field("SpellFlags", spec.flags);
    }

    return true;
}

nlohmann::json CombatInfo::to_json() const
{
    nlohmann::json j;
    j["ac_natural"] = ac_natural;

    auto& arr = j["special_abilities"] = nlohmann::json::array();
    for (const auto& sa : special_abilities) {
        arr.push_back({
            {"spell", sa.spell},
            {"level", sa.level},
            {"flags", sa.flags},
        });
    }

    return j;
}

} // namespace nw
