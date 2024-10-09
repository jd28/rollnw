#pragma once

#include "../rules/Class.hpp"
#include "../rules/attributes.hpp"
#include "../rules/feats.hpp"

namespace nw {

struct LevelUp {

    bool epic = false;
    Class class_ = Class::invalid();
    Ability ability = Ability::invalid();
    int hitpoints = 0;
    int skillpoints = 0;
    Vector<Feat> feats;
    Vector<int32_t> skills;
    Vector<std::pair<int32_t, Spell>> known_spells;
};

void from_json(const nlohmann::json& json, LevelUp& entry);
void to_json(nlohmann::json& json, const LevelUp& entry);

/// Encapsulates a players level up history
struct LevelHistory {
    Vector<LevelUp> entries;
};

bool deserialize(LevelUp& self, const GffStruct& archive);
bool serialize(const LevelUp& self, GffBuilderStruct& archive);

} // namespace nw
