#pragma once

#include "../serialization/Archives.hpp"

#include <vector>

namespace nw {

struct FactionInfo {
    std::string name;
    uint32_t parent = std::numeric_limits<uint32_t>::max();
    uint16_t global = 0;
};

struct Reputation {
    uint32_t faction_1;
    uint32_t faction_2;
    uint32_t reputation;
};

struct Faction {
    explicit Faction(const Gff& archive);
    explicit Faction(const nlohmann::json& archive);

    static constexpr int json_archive_version = 1;
    static constexpr ResourceType::type restype = ResourceType::fac;

    GffBuilder serialize() const;
    nlohmann::json to_json() const;

    std::vector<FactionInfo> factions;
    std::vector<Reputation> reputations;
};

} // namespace nw
