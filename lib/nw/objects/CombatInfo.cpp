#include "CombatInfo.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool CombatInfo::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("ac_natural").get_to(ac_natural_bonus);

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

nlohmann::json CombatInfo::to_json() const
{
    nlohmann::json j;
    j["ac_natural"] = ac_natural_bonus;

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

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(CombatInfo& self, const GffStruct& archive)
{
    uint8_t temp;
    if (archive.get_to("NaturalAC", temp)) {
        self.ac_natural_bonus = temp;
    }

    size_t sz = archive["SpecAbilityList"].size();
    self.special_abilities.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        SpecialAbility sa;
        archive["SpecAbilityList"][i].get_to("Spell", sa.spell);
        archive["SpecAbilityList"][i].get_to("SpellCasterLevel", sa.level);
        archive["SpecAbilityList"][i].get_to("SpellFlags", sa.flags);
        self.special_abilities.push_back(sa);
    }
    return true;
}

bool serialize(const CombatInfo& self, GffBuilderStruct& archive)
{
    archive.add_field("NaturalAC", uint8_t(self.ac_natural_bonus));
    auto& list = archive.add_list("SpecAbilityList");
    for (const auto& spec : self.special_abilities) {
        list.push_back(4)
            .add_field("Spell", spec.spell)
            .add_field("SpellCasterLevel", spec.level)
            .add_field("SpellFlags", spec.flags);
    }

    return true;
}
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
