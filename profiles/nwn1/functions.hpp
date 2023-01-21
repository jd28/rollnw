#pragma once

#include "helpers.hpp"

#include <nw/objects/Equips.hpp>

namespace nwn1 {

// == Abilities ===============================================================
// ============================================================================

/// Gets creatures ability score
int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false);

/// Gets creatures ability modifier
int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false);

/// Gets creatures dexterity modifier as modified by armor, etc.
int get_dex_modifier(const nw::Creature* obj);

// == Armor Class =============================================================
// ============================================================================

/// Calculate Armor Class versus another object
int calculate_ac_versus(const nw::ObjectBase* obj, const nw::ObjectBase* versus, bool is_touch_attack);

/// Calculates the armor class of a piece of armor
int calculate_item_ac(const nw::Item* obj);

// == Classes =================================================================
// ============================================================================

/// Determines if monk class abilities are usable and monk class level
std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj);

// == Effects =================================================================
// ============================================================================

/// Determines if object has effect type applied
bool has_effect_type_applied(nw::ObjectBase* obj, nw::EffectType type);

/// Queues remove effect events by effect creator
int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator);

// == Hit Points ==============================================================
// ============================================================================

/// Gets objects current hitpoints
int get_current_hitpoints(const nw::ObjectBase* obj);

/// Gets objects maximum hit points.
int get_max_hitpoints(const nw::ObjectBase* obj);

// == Items ===================================================================
// ============================================================================

/// Determines if an item can be equipped
bool can_equip_item(const nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Equip an item
bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Gets an equipped item
nw::Item* get_equipped_item(const nw::Creature* obj, nw::EquipIndex slot);

/// Determines if item is a shield
bool is_shield(nw::BaseItem baseitem);

/// Unequips an item
nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot);

// == Saves ====================================================================
// =============================================================================

int saving_throw(const nw::ObjectBase* obj, nw::Save type, nw::SaveVersus type_vs = nw::SaveVersus::invalid(),
    const nw::ObjectBase* versus = nullptr);

bool resolve_saving_throw(const nw::ObjectBase* obj, nw::Save type, int dc,
    nw::SaveVersus type_vs = nw::SaveVersus::invalid(), const nw::ObjectBase* versus = nullptr);

// == Skills ==================================================================
// ============================================================================

/// Determines creatures skill rank
int get_skill_rank(const nw::Creature* obj, nw::Skill skill,
    nw::ObjectBase* versus = nullptr, bool base = false);

bool resolve_skill_check(const nw::Creature* obj, nw::Skill skill, int dc,
    nw::ObjectBase* versus = nullptr);

// == Weapons =================================================================
// ============================================================================

/// Converts an equip index to an attack type
nw::AttackType equip_index_to_attack_type(nw::EquipIndex equip);

/// Gets relative weapon size
/// @note May or may not be what the game does
int get_relative_weapon_size(const nw::Creature* obj, const nw::Item* item);

/// Gets an equipped weapon by attack type
nw::Item* get_weapon_by_attack_type(const nw::Creature* obj, nw::AttackType type);

/// Determines if item is creature weapon
bool is_creature_weapon(const nw::Item* item);

/// Determines if item is double sided weapon
bool is_double_sided_weapon(const nw::Item* item);

/// Determines if item is a light weapon
bool is_light_weapon(const nw::Creature* obj, const nw::Item* item);

/// Determines if item is monk weapon
bool is_monk_weapon(const nw::Item* item);

/// Determines if item is ranged weapon
bool is_ranged_weapon(const nw::Item* item);

/// Determines if item is unarmed weapon
bool is_unarmed_weapon(const nw::Item* item);

/// Determines if item requires two hands to wield
bool is_two_handed_weapon(const nw::Creature* obj, const nw::Item* item);

/// Resolves creature weapon damage
nw::DiceRoll resolve_creature_damage(const nw::Creature* attacker, nw::Item* weapon);

/// Resolve unarmed base damage
nw::DiceRoll resolve_unarmed_damage(const nw::Creature* attacker);

/// Resolve weapon base damage
/// @note Includes specialization and arcane archer bonuses
nw::DiceRoll resolve_weapon_damage(const nw::Creature* attacker, nw::BaseItem item);

} // namespace nwn1
