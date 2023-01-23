#pragma once

#include "../rules/combat.hpp"
#include "../serialization/Archives.hpp"
#include "SpellBook.hpp"

namespace nw {

// Forward Decls
struct ObjectBase;

struct SpecialAbility {
    uint16_t spell;
    uint8_t level;
    SpellFlags flags = SpellFlags::none;
};

struct CombatInfo {
    CombatInfo() = default;
    CombatInfo(CombatInfo&) = default;
    CombatInfo(CombatInfo&&) = default;

    CombatInfo& operator=(CombatInfo&) = delete;
    CombatInfo& operator=(CombatInfo&&) = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    // Serialized Blueprint
    int ac_natural_bonus = 0;

    // Transient

    /// Current attack counter.
    /// @note Invariant: attack_current <= attacks_onhand + attacks_offhand + attacks_extra
    int attack_current = 0;
    int attacks_onhand = 0;
    int attacks_offhand = 0;
    int attacks_extra = 0;

    nw::ObjectBase* target = nullptr;
    /// Distance to target squared
    float target_distance_sq = 0.0f;
    TargetState target_state = TargetState::none;

    int ac_armor_base = 0;
    int ac_shield_base = 0;
    CombatMode combat_mode = nw::CombatMode::invalid();
    int32_t size_ab_modifier = 0;
    int32_t size_ac_modifier = 0;

    std::vector<SpecialAbility> special_abilities;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(CombatInfo& self, const GffStruct& archive);
bool serialize(const CombatInfo& self, GffBuilderStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
