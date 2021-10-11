#pragma once

#include "../formats/Gff.hpp"

#include <vector>

namespace nw {

struct FactionInfo {
    std::string name;
    uint32_t parent = ~0;
    uint16_t global = 0;
};

struct Reputation {
    uint32_t faction_1;
    uint32_t faction_2;
    uint32_t reputation;
};

struct Faction {
    explicit Faction(const GffStruct gff);

    std::vector<FactionInfo> factions;
    std::vector<Reputation> reputations;
};

} // namespace nw
