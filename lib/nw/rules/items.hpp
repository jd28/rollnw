#pragma once

#include "../resources/Resource.hpp"
#include "../util/Variant.hpp"
#include "Dice.hpp"
#include "rule_type.hpp"
#include "system.hpp"

#include <absl/container/inlined_vector.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace nw {

struct TwoDA;
struct TwoDARowView;

DECLARE_RULE_TYPE(BaseItem);

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

DECLARE_RULE_TYPE(ItemPropertyType);

struct ItemProperty {
    uint16_t type = std::numeric_limits<uint16_t>::max();
    uint16_t subtype = std::numeric_limits<uint16_t>::max();
    uint8_t cost_table = std::numeric_limits<uint8_t>::max();
    uint16_t cost_value = std::numeric_limits<uint16_t>::max();
    uint8_t param_table = std::numeric_limits<uint8_t>::max();
    uint8_t param_value = std::numeric_limits<uint8_t>::max();
};

struct ItemPropertyDefinition {
    uint32_t name = std::numeric_limits<uint32_t>::max();
    const TwoDA* subtype = nullptr;
    float cost = 0.0f;
    const TwoDA* cost_table = nullptr;
    const TwoDA* param_table = nullptr;
    uint32_t game_string = std::numeric_limits<uint32_t>::max();
    uint32_t description = std::numeric_limits<uint32_t>::max();
    // Constants
};

enum struct WeaponModifierType {
    attack_bonus,
    crit_damage,
    crit_mult,
    crit_threat,
    damage,
};

struct WeaponModifier {
    WeaponModifierType type;
    int feat = -1;
    Variant<int, float, DiceRoll> value;
};

bool operator==(const WeaponModifier& lhs, const WeaponModifier& rhs);
bool operator<(const WeaponModifier& lhs, const WeaponModifier& rhs);

struct BaseItemInfo {
    BaseItemInfo() = default;
    BaseItemInfo(const TwoDARowView& tda);

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
    int weapon_wield = 0;
    int weapon_type = 0;
    int weapon_size = 0;
    uint32_t ranged = 0;
    // PrefAttackDist
    // MinRange
    // MaxRange
    nw::DiceRoll base_damage;
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
    ArmorClass ac_type = ArmorClass::invalid();
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

    // Don't care about the weapon feats, these will be loaded in to the masterfeat system

    bool is_monk_weapon = false;
    int finesse_size = 3;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// BaseItem singleton component
using BaseItemArray = RuleTypeArray<BaseItem, BaseItemInfo>;

} // namespace nw
