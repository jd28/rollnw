#pragma once

#include "constants.hpp"

#include <nw/objects/Equips.hpp>

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

/// Calculates base attack bonus
int base_attack_bonus(const nw::Creature* obj);

/// Determine if creature is flanked by an attacker
bool is_flanked(const nw::Creature* target, const nw::Creature* attacker);

/// Resolves an attack
/// @note All transient book keeping is done at the toplevel of this function,
/// any other that attacker and/or target are passed to, are passed as const.
std::unique_ptr<nw::AttackData> resolve_attack(nw::Creature* attacker, nw::ObjectBase* target);

/// Resolves attack bonus
int resolve_attack_bonus(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* versus = nullptr);

/// Resolves damage from an attack
/// @return Total damage, ``data`` holds individual damage totals
int resolve_attack_damage(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data);

/// Resolves an attack roll
nw::AttackResult resolve_attack_roll(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs, nw::AttackData* data = nullptr);

/// Resolves attack type
nw::AttackType resolve_attack_type(const nw::Creature* obj);

/// Resolves an concealment - i.e. the highest of concealment and miss chance
/// @return Concealment amount, bool that if ``true`` is from attacking creature i.e miss chance,
/// if ``false`` from target object i.e. concealment
std::pair<int, bool> resolve_concealment(const nw::ObjectBase* obj, const nw::ObjectBase* target, bool vs_ranged = false);

/// Resolves critical multiplier
int resolve_critical_multiplier(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs = nullptr);

/// Resolves critical threat range.
int resolve_critical_threat(const nw::Creature* obj, nw::AttackType type);

/// Resolves damage modifiers - soak, resist, immunity
void resolve_damage_modifiers(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data);

/// Resolves damage immunity
int resolve_damage_immunity(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus = nullptr);

/// Resolves damage reduction
std::pair<int, nw::Effect*> resolve_damage_reduction(const nw::ObjectBase* obj, int power, const nw::ObjectBase* versus = nullptr);

/// Resolves damage resistance
std::pair<int, nw::Effect*> resolve_damage_resistance(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus = nullptr);

/// Resolves dual-wield penalty
std::pair<int, int> resolve_dual_wield_penalty(const nw::Creature* obj);

/// Resolves iteration penalty
int resolve_iteration_penalty(const nw::Creature* attacker, nw::AttackType type);

/// Resolves number of attacks
std::pair<int, int> resolve_number_of_attacks(const nw::Creature* obj);

/// Resolve target state
nw::TargetState resolve_target_state(const nw::Creature* attacker, const nw::ObjectBase* target);

/// Resolve weapon base damage flags
nw::DamageFlag resolve_weapon_damage_flags(const nw::Item* weapon);

/// Resolves weapon power
int resolve_weapon_power(const nw::Creature* obj, const nw::Item* weapon);

/// Determines if a weapon is finessable
bool weapon_is_finessable(const nw::Creature* obj, nw::Item* weapon);

/// Calculates weapon iteration, e.g. 5 or 3 for monk weapons
int weapon_iteration(const nw::Creature* obj, const nw::Item* weapon);

} // namespace nwn1
