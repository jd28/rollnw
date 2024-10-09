#pragma once

#include "../rules/attributes.hpp"
#include "../rules/feats.hpp"
#include "../serialization/Serialization.hpp"
#include "Saves.hpp"

#include <array>

namespace nw {

struct CreatureStats {
    CreatureStats() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    /// Attempts to add a feat to a creature, returning true if successful
    bool add_feat(Feat id);

    /// Gets the feat array
    const Vector<Feat>& feats() const noexcept;

    /// Gets an ability score
    int get_ability_score(Ability id) const;

    /// Gets a skill rank
    int get_skill_rank(Skill id) const;

    /// Determines if creature has a feat
    bool has_feat(Feat id) const noexcept;

    /// Removes a feat
    /// @todo This function removes the feat, as likely the game does. But should it remove feats that require this feat?
    void remove_feat(Feat id);

    /// Sets an ability score, returning true if successful
    bool set_ability_score(Ability id, int value);

    /// Sets a skill rank, returning true if successful
    bool set_skill_rank(Skill id, int value);

    Saves save_bonus;

    friend bool deserialize(CreatureStats& self, const GffStruct& archive);
    friend bool serialize(const CreatureStats& self, GffBuilderStruct& archive);

private:
    std::array<uint8_t, 6> abilities_;
    Vector<Feat> feats_;
    Vector<uint8_t> skills_;
};

bool deserialize(CreatureStats& self, const GffStruct& archive);
bool serialize(const CreatureStats& self, GffBuilderStruct& archive);

} // namespace nw
