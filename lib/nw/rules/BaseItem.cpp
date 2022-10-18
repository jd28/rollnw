#include "BaseItem.hpp"

#include "../formats/TwoDA.hpp"
#include "../kernel/Kernel.hpp"

namespace nw {

BaseItemInfo::BaseItemInfo(const TwoDARowView& tda)
{
    std::string temp_string;
    int temp_int;

    if (!tda.get_to("label", temp_string)) { return; }

    tda.get_to("Name", name);
    tda.get_to("InvSlotWidth", inventory_slot_size.first);
    tda.get_to("InvSlotHeight", inventory_slot_size.second);
    tda.get_to("EquipableSlots", equipable_slots);
    tda.get_to("CanRotateIcon", can_rotate_icon);
    // ModelType
    // ItemClass
    tda.get_to("GenderSpecific", gender_specific);
    tda.get_to("Part1EnvMap", std::get<0>(composite_env_map));
    tda.get_to("Part2EnvMap", std::get<1>(composite_env_map));
    tda.get_to("Part3EnvMap", std::get<2>(composite_env_map));
    if (tda.get_to("DefaultModel", temp_string)) {
        default_model = {temp_string, nw::ResourceType::mdl};
    }
    tda.get_to("DefaultIcon", default_icon);
    tda.get_to("Container", is_container);
    // WeaponWield
    // WeaponType
    // WeaponSize
    tda.get_to("RangedWeapon", ranged);
    // PrefAttackDist
    // MinRange
    // MaxRange
    // NumDice
    // DieToRoll
    tda.get_to("CritThreat", crit_threat);
    tda.get_to("CritHitMult", crit_multiplier);
    // Category
    // BaseCost
    // Stacking
    // ItemMultiplier
    tda.get_to("Description", description);
    // InvSoundType
    // MaxProps
    // MinProps
    // PropColumn
    // StorePanel

    // AC_Enchant
    // BaseAC
    // ArmorCheckPen
    // BaseItemStatRef
    // ChargesStarting
    // RotateOnGround
    // TenthLBS
    // WeaponMatType
    // AmmunitionType
    // QBBehaviour
    // ArcaneSpellFailure
    // %AnimSlashL
    // %AnimSlashR
    // %AnimSlashS
    // StorePanelSort
    // ILRStackSize

    if (tda.get_to("WeaponFocusFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("EpicWeaponFocusFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("WeaponSpecializationFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("EpicWeaponSpecializationFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("WeaponImprovedCriticalFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("EpicWeaponOverwhelmingCriticalFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("EpicWeaponDevastatingCriticalFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    if (tda.get_to("WeaponOfChoiceFeat", temp_int)) {
        weapon_modifiers_feats.push_back(temp_int);
    }

    std::sort(std::begin(weapon_modifiers_feats),
        std::end(weapon_modifiers_feats));

    tda.get_to("IsMonkWeapon", is_monk_weapon);
    // WeaponFinesseMinimumCreatureSize
}

const BaseItemInfo* BaseItemArray::get(BaseItem baseitem) const noexcept
{
    size_t idx = static_cast<size_t>(baseitem);
    if (idx < entries.size() && entries[idx].valid()) {
        return &entries[idx];
    }
    return nullptr;
}

bool BaseItemArray::is_valid(BaseItem baseitem) const noexcept
{
    size_t idx = static_cast<size_t>(baseitem);
    return idx < entries.size() && entries[idx].valid();
}

BaseItem BaseItemArray::from_constant(std::string_view constant) const
{
    absl::string_view v{constant.data(), constant.size()};
    auto it = constant_to_index.find(v);
    if (it == constant_to_index.end()) {
        return BaseItem::invalid();
    } else {
        return it->second;
    }
}

} // namespace nw
