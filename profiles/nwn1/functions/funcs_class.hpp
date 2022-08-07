#pragma once

#include <utility>

namespace nw {
struct Creature;
} // namespace nw

namespace nwn1 {

/// Determines if monk class abilities are usable and monk class level
std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj);

} // namespace nw
