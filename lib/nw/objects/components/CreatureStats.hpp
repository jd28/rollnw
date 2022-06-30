#pragma once

#include "../../rules/Feat.hpp"
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

    bool add_feat(Feat feat);
    const std::vector<Feat>& feats() const noexcept;
    bool has_feat(Feat feat) const noexcept;

    std::array<uint8_t, 6> abilities;
    std::vector<uint8_t> skills;
    Saves save_bonus;

private:
    std::vector<Feat> feats_;
};

} // namespace nw
