#pragma once

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

int calculate_ac_versus(const nw::Creature* obj, const nw::ObjectBase* versus, bool is_touch_attack);

/// Calculates the armor class of a piece of armor
int calculate_ac(const nw::Item* obj);

} // namespace nwn1
