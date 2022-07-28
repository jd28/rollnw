#pragma once

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

/// Calculates attack bonus
int attack_bonus(const nw::Creature* obj, bool base = false);

/// Number of attacks per second
float attacks_per_second(const nw::Creature* obj, const nw::ObjectBase* vs);

/// Calculates number of attacks
int number_of_attacks(const nw::Creature* obj, bool offhand = false);

/// Calculates weapon iteration, e.g. 5 or 3 for monk weapons
int weapon_iteration(const nw::Creature* obj, const nw::Item* weapon);

} // namespace nwn1
