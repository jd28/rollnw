#pragma once

#include "../rules/attributes.hpp"
#include "../rules/feats.hpp"
#include "../serialization/Archives.hpp"
#include "Saves.hpp"

#include <array>
#include <vector>

namespace nw {

struct CreatureStats {
    CreatureStats() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    /// Attempts to add a feat to a creature, returning true if successful
    bool add_feat(Feat id);

    /// Gets the feat array
    const std::vector<Feat>& feats() const noexcept;

    /// Gets an ability score
    int get_ability_score(Ability id) const;

    /// Gets a skill rank
    int get_skill_rank(Skill id) const;

    /// Determines if creature has a feat
    bool has_feat(Feat id) const noexcept;

    /// Sets an ability score, returning true if successful
    bool set_ability_score(Ability id, int value);

    /// Sets a skill rank, returning true if successful
    bool set_skill_rank(Skill id, int value);

    Saves save_bonus;

    friend bool deserialize(CreatureStats& self, const GffStruct& archive);
    friend bool serialize(const CreatureStats& self, GffBuilderStruct& archive);

private:
    std::array<uint8_t, 6> abilities_;
    std::vector<Feat> feats_;
    std::vector<uint8_t> skills_;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(CreatureStats& self, const GffStruct& archive);
bool serialize(const CreatureStats& self, GffBuilderStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
