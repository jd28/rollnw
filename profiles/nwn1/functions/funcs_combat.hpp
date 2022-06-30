#pragma once

#include <nw/util/EntityView.hpp>

#include <flecs/flecs.h>

namespace nw {
struct Item;
} // namespace nw

namespace nwn1 {

/// Calculates attack bonus
int attack_bonus(flecs::entity ent, bool base = false);

/// Number of attacks per second
float attacks_per_second(flecs::entity ent, flecs::entity vs);

/// Calculates number of attacks
int number_of_attacks(flecs::entity ent, bool offhand = false);

/// Calculates weapon iteration, e.g. 5 or 3 for monk weapons
int weapon_iteration(flecs::entity ent, nw::EntityView<nw::Item> weapon);

} // namespace nwn1
