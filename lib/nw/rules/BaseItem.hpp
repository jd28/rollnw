#pragma once

#include "../resources/Resource.hpp"
#include "../util/Variant.hpp"
#include "Dice.hpp"
#include "system.hpp"

#include <absl/container/inlined_vector.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace nw {

struct TwoDARowView;

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

enum struct WeaponModiferType {
    attack_bonus,
    crit_damage,
    crit_mult,
    crit_threat,
    damage,
};

struct WeaponModifier {
    WeaponModiferType type;
    int feat = -1;
    Variant<int, float, DiceRoll> value;
};

bool operator==(const WeaponModifier& lhs, const WeaponModifier& rhs);
bool operator<(const WeaponModifier& lhs, const WeaponModifier& rhs);

struct BaseItem {
    BaseItem() = default;
    BaseItem(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    std::pair<int, int> inventory_slot_size;
    uint32_t equipable_slots = 0;
    bool can_rotate_icon = false;
    ItemModelType model_type;
    // ItemClass
    bool gender_specific = false;
    std::tuple<bool, bool, bool> composite_env_map;
    Resource default_model;
    std::string default_icon;
    bool is_container = false;
    // WeaponWield
    // WeaponType
    // WeaponSize
    bool ranged = false;
    // PrefAttackDist
    // MinRange
    // MaxRange
    // NumDice
    // DieToRoll
    int crit_threat = 0;
    int crit_multiplier = 0;
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
    Requirement feat_requirement{{}, false};
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

    // Don't care what these feats represent, could be Weapon Focus, could be Dev Crit, etc.
    // How they actually affect combat will be dealt with by their Master Feats in a different
    // part of the rules system.
    absl::InlinedVector<int, 8> weapon_modifiers_feats;

    bool is_monk_weapon = false;
    // WeaponFinesseMinimumCreatureSize
};

/// BaseItem singleton component
struct BaseItemArray {
    std::vector<BaseItem> entries;
};

} // namespace nw
