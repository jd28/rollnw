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

/// Queues remove effect events by effect creator
int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator);

// == Items ===================================================================
// ============================================================================

/// Determines if an item can be equipped
bool can_equip_item(const nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Equip an item
bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Gets an equipped item
nw::Item* get_equipped_item(const nw::Creature* obj, nw::EquipIndex slot);

/// Determines if weapon is ranged
bool is_ranged_weapon(const nw::Item* item);

/// Determines if item is a shield
bool is_shield(nw::BaseItem baseitem);

/// Unequips an item
nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot);

// == Skills ==================================================================
// ============================================================================

/// Determines creatures skill rank
int get_skill_rank(const nw::Creature* obj, nw::Skill skill,
    nw::ObjectBase* versus = nullptr, bool base = false);

} // namespace nwn1
