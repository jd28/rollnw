#pragma once

#include "constants.hpp"

#include <nw/objects/Equips.hpp>

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

/// Calculates attack bonus
int attack_bonus(const nw::Creature* obj, nw::AttackType type, nw::ObjectBase* versus = nullptr);

/// Number of attacks per second
float attacks_per_second(const nw::Creature* obj, const nw::ObjectBase* vs);

/// Calculates base attack bonus
int base_attack_bonus(const nw::Creature* obj);

/// Converts an equip index to an attack type
nw::AttackType equip_index_to_attack_type(nw::EquipIndex equip);

/// Calculates number of attacks
int number_of_attacks(const nw::Creature* obj, bool offhand = false);

/// Gets an equipped weapon by attack type
nw::Item* get_weapon_by_attack_type(const nw::Creature* obj, nw::AttackType type);

/// Resolves an concealment - i.e. the highest of concealment and miss chance
/// @return Concealment amount, bool that if ``true`` is from attacking creature i.e miss chance,
/// if ``false`` from target object i.e. concealment
std::pair<int, bool> resolve_concealment(const nw::ObjectBase* obj, const nw::ObjectBase* target, bool vs_ranged = false);

/// Determines if a weapon is finessable
bool weapon_is_finessable(const nw::Creature* obj, nw::Item* weapon);

/// Calculates weapon iteration, e.g. 5 or 3 for monk weapons
int weapon_iteration(const nw::Creature* obj, const nw::Item* weapon);

} // namespace nwn1
