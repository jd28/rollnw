#pragma once

#include "../rules/Class.hpp"
#include "../rules/attributes.hpp"
#include "../rules/feats.hpp"

#include <vector>

namespace nw {

struct LevelUp {

    bool epic = false;
    Class class_ = Class::invalid();
    Ability ability = Ability::invalid();
    int hitpoints = 0;
    int skillpoints = 0;
    std::vector<Feat> feats;
    std::vector<int32_t> skills;
    std::vector<std::pair<int32_t, Spell>> known_spells;
};

void from_json(const nlohmann::json& json, LevelUp& entry);
void to_json(nlohmann::json& json, const LevelUp& entry);

/// Encapsulates a players level up history
struct LevelHistory {
    std::vector<LevelUp> entries;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(LevelUp& self, const GffStruct& archive);
bool serialize(const LevelUp& self, GffBuilderStruct& archive);
#endif

} // namespace nw
