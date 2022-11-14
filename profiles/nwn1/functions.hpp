#pragma once

#include "functions/funcs_ability.hpp"
#include "functions/funcs_armor_class.hpp"
#include "functions/funcs_class.hpp"
#include "functions/funcs_combat.hpp"
#include "functions/funcs_effects.hpp"
#include "functions/funcs_feat.hpp"
#include "functions/funcs_skill.hpp"
#include "functions/rule_helper_funcs.hpp"

namespace nwn1 {

/// Determines if an item can be equipped
bool can_equip_item(const nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Equip an item
bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Processes item properties and applies resulting effects to creature
int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove);

/// Gets an equipped item
nw::Item* get_equipped_item(const nw::Creature* obj, nw::EquipIndex slot);

/// Converts item property to in-game style string
std::string itemprop_to_string(const nw::ItemProperty& ip);

/// Queues remove effect events by effect creator
int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator);

/// Unequips an item
nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot);

} // namespace nwn1
