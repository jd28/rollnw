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
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    bool add_feat(size_t feat);
    const std::vector<uint16_t>& feats() const noexcept;
    bool has_feat(size_t feat) const noexcept;

    std::array<uint8_t, 6> abilities;
    std::vector<uint8_t> skills;
    Saves save_bonus;

private:
    std::vector<uint16_t> feats_;
};

} // namespace nw
