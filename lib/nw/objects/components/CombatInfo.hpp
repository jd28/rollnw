#pragma once

#include "../../serialization/Archives.hpp"
#include "SpellBook.hpp"

namespace nw {

struct SpecialAbility {
    uint16_t spell;
    uint8_t level;
    SpellFlags flags = SpellFlags::none;
};

struct CombatInfo {
    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    uint8_t ac_natural = 0;
    std::vector<SpecialAbility> special_abilities;
};

} // namespace nw
