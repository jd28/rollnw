#pragma once

#include "../resources/ResourceType.hpp"
#include "../serialization/Serialization.hpp"

#include <string>
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

    bool operator<(const Reputation& rhs)
    {
        return std::tie(faction_1, faction_2) < std::tie(rhs.faction_1, rhs.faction_2);
    }
};

struct Faction {
    explicit Faction(const Gff& archive);
    explicit Faction(const nlohmann::json& archive);

    static constexpr int json_archive_version = 1;
    static constexpr ResourceType::type restype = ResourceType::fac;

    nlohmann::json to_json() const;

    std::vector<FactionInfo> factions;
    std::vector<Reputation> reputations;
};

// == Faction - Serialization - Gff ==========================================
// ============================================================================

bool deserialize(Faction& obj, const GffStruct& archive);
GffBuilder serialize(const Faction& obj);

// == Faction - Serialization - JSON ==========================================
// ============================================================================

bool deserialize(Faction& obj, const nlohmann::json& archive);

} // namespace nw
