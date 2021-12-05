#pragma once

#include "../../serialization/Serialization.hpp"
#include "Saves.hpp"

#include <array>
#include <vector>

namespace nw {

struct CreatureStats {
    CreatureStats() = default;
    CreatureStats(const GffStruct gff);

    std::array<uint8_t, 6> abilities;
    std::vector<uint16_t> feats;
    std::vector<uint8_t> skills;
    Saves save_bonus;
};

} // namespace nw
