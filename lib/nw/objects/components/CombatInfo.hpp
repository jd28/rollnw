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
    CombatInfo() = default;
    CombatInfo(CombatInfo&) = default;
    CombatInfo(CombatInfo&&) = default;

    CombatInfo& operator=(CombatInfo&) = delete;
    CombatInfo& operator=(CombatInfo&&) = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    uint8_t ac_natural = 0;
    std::vector<SpecialAbility> special_abilities;
};

} // namespace nw
