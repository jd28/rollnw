#pragma once

#include "functions.hpp"

#include <nw/objects/ObjectBase.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

// == Modifiers ===============================================================
// ============================================================================

void load_modifiers();

// Generic
nw::ModifierFunction simple_feat_mod(nw::Feat feat, int value);

// Ability
nw::ModifierResult class_stat_gain(const nw::ObjectBase* obj, int32_t subtype);
nw::ModifierResult epic_great_ability(const nw::ObjectBase* obj, int32_t subtype);

// Armor Class
nw::ModifierResult dragon_disciple_ac(const nw::ObjectBase* obj);
nw::ModifierResult pale_master_ac(const nw::ObjectBase* obj);
nw::ModifierResult training_versus_ac(const nw::ObjectBase* obj, const nw::ObjectBase* target);
nw::ModifierResult tumble_ac(const nw::ObjectBase* obj);

// Attack Bonus
nw::ModifierResult ability_attack_bonus(const nw::ObjectBase* obj, int32_t subtype);
nw::ModifierResult combat_mode_ab(const nw::ObjectBase* obj);
nw::ModifierResult enchant_arrow_ab(const nw::ObjectBase* obj, int32_t subtype);
nw::ModifierResult favored_enemy_ab(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype);
nw::ModifierResult good_aim(const nw::ObjectBase* obj, int32_t subtype);
nw::ModifierResult target_state_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target);
nw::ModifierResult training_versus_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target);
nw::ModifierResult weapon_feat_ab(const nw::ObjectBase* obj, int32_t subtype);
nw::ModifierResult weapon_master_ab(const nw::ObjectBase* obj, int32_t subtype);

// Concealment
nw::ModifierResult epic_self_concealment(const nw::ObjectBase* obj);

// Damage Bonus
nw::ModifierResult ability_damage(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype);
nw::ModifierResult combat_mode_dmg(const nw::ObjectBase* obj);
nw::ModifierResult favored_enemy_dmg(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype);
nw::ModifierResult overwhelming_crit_dmg(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype);

// Damage Immunity
nw::ModifierResult dragon_disciple_immunity(const nw::ObjectBase* obj, int32_t subtype);

// Damage Reduction
nw::ModifierResult dwarven_defender_dmg_reduction(const nw::ObjectBase* obj);
nw::ModifierResult barbarian_dmg_reduction(const nw::ObjectBase* obj);
nw::ModifierResult epic_dmg_reduction(const nw::ObjectBase* obj);

// Damage Resist
nw::ModifierResult energy_resistance(const nw::ObjectBase* obj, int32_t subtype);

// Hitpoints
nw::ModifierResult toughness(const nw::ObjectBase* obj);
nw::ModifierResult epic_toughness(const nw::ObjectBase* obj);

}
