#pragma once

#include "../../serialization/Archives.hpp"
#include "Saves.hpp"

#include <array>
#include <vector>

namespace nw {

struct CreatureStats {
    CreatureStats() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    std::array<uint8_t, 6> abilities;
    std::vector<uint16_t> feats;
    std::vector<uint8_t> skills;
    Saves save_bonus;
};

} // namespace nw
