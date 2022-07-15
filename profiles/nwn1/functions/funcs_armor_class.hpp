#pragma once

#include <nw/components/Item.hpp>
#include <nw/util/EntityView.hpp>

namespace nwn1 {

int calculate_ac_versus(flecs::entity ent, flecs::entity versus, bool is_touch_attack);

int calculate_ac(nw::EntityView<nw::Item> ent);

} // namespace nwn1
