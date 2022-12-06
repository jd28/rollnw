#pragma once

#include "../rules/combat.hpp"
#include "../serialization/Archives.hpp"
#include "SpellBook.hpp"

namespace nw {

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

    bool from_gff(const GffStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffBuilderStruct& archive) const;
    nlohmann::json to_json() const;

    // Serialized Blueprint
    int ac_natural_bonus = 0;

    // Transient
    int ac_armor_base = 0;
    int ac_shield_base = 0;
    CombatMode combat_mode = nw::CombatMode::invalid();
    TargetState target_state = TargetState::none;
    int32_t size_ab_modifier = 0;
    int32_t size_ac_modifier = 0;

    std::vector<SpecialAbility> special_abilities;
};

} // namespace nw
