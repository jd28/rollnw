#include "items.hpp"

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
    tda.get_to("WeaponWield", weapon_wield);
    tda.get_to("WeaponType", weapon_type);
    tda.get_to("WeaponSize", weapon_size);
    tda.get_to("RangedWeapon", ranged);
    // PrefAttackDist
    // MinRange
    // MaxRange
    tda.get_to("NumDice", base_damage.dice);
    tda.get_to("DieToRoll", base_damage.sides);
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

    if (tda.get_to("AC_Enchant", temp_int)) {
        ac_type = nw::ArmorClass::make(temp_int);
    }
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

    tda.get_to("IsMonkWeapon", is_monk_weapon);
    tda.get_to("WeaponFinesseMinimumCreatureSize", finesse_size);
}

} // namespace nw
