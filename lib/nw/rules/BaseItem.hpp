#pragma once

#include "../resources/Resource.hpp"

#include <cstdint>
#include <utility>

namespace nw {

enum struct ItemModelType : uint8_t {
    simple,
    layered,
    composite,
    armor
};

struct ItemColors {
    enum type : uint8_t {
        cloth1 = 0,
        cloth2 = 1,
        leather1 = 2,
        leather2 = 3,
        metal1 = 4,
        metal2 = 5,
    };
};

struct ItemModelParts {
    enum type : uint8_t {
        model1 = 0,
        model2 = 1,
        model3 = 2,

        armor_belt = model1,
        armor_lbicep = model2,
        armor_lfarm = model3,
        armor_lfoot = 3,
        armor_lhand = 4,
        armor_lshin = 5,
        armor_lshoul = 6,
        armor_lthigh = 7,
        armor_neck = 8,
        armor_pelvis = 9,
        armor_rbicep = 10,
        armor_rfarm = 11,
        armor_rfoot = 12,
        armor_rhand = 13,
        armor_robe = 14,
        armor_rshin = 15,
        armor_rshoul = 16,
        armor_rthigh = 17,
        armor_torso = 18
    };
};

struct BaseItem {
    uint32_t name = 0xFFFFFFFF;
    std::pair<int, int> inventory_slot_size;
    uint32_t equipable_slots = 0;
    bool can_rotate_icon = false;
    ItemModelType model_type;
    // ItemClass
    bool gender_specific = false;
    std::tuple<bool, bool, bool> composite_env_map;
    // Part1EnvMap, Part2EnvMap, Part3EnvMap
    Resource default_model;
    std::string default_icon;
    bool is_container = false;
    // WeaponWield
    // WeaponType
    // WeaponSize
    bool ranged = false; // RangedWeapon
    // PrefAttackDist
    // MinRange
    // MaxRange
    // NumDice
    // DieToRoll
    // CritThreat
    // CritHitMult
    // Category
    // BaseCost
    // Stacking
    // ItemMultiplier
    uint32_t description = 0xFFFFFFFF;
    // InvSoundType
    // MaxProps
    // MinProps
    // PropColumn
    // StorePanel
    // ReqFeat0, ReqFeat1, ReqFeat2, ReqFeat3, ReqFeat4
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
    // WeaponFocusFeat
    // EpicWeaponFocusFeat
    // WeaponSpecializationFeat
    // EpicWeaponSpecializationFeat
    // WeaponImprovedCriticalFeat
    // EpicWeaponOverwhelmingCriticalFeat
    // EpicWeaponDevastatingCriticalFeat
    // WeaponOfChoiceFeat
    bool is_monk_weapon = false;
    // WeaponFinesseMinimumCreatureSize
};

} // namespace nw
