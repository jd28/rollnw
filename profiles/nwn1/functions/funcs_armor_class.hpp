#pragma once

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

int calculate_ac_versus(const nw::Creature* obj, const nw::ObjectBase* versus, bool is_touch_attack);

// int calculate_ac(const nw::Item* obj);

} // namespace nwn1
