#pragma once

#include "ObjectBase.hpp"
#include "Serialization.hpp"
#include "components/Common.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace nw {

struct SpawnCreature {
    int32_t appearance;
    float cr;
    Resref resref;
    uint8_t single_spawn;
};

struct SpawnPoint {
    float orientation; // In rad
    glm::vec3 position;
};

struct Encounter : public ObjectBase {
    Encounter(const GffStruct gff, SerializationProfile profile);
    virtual Common* common() { return &common_; }

    Common common_;
    std::vector<SpawnCreature> creatures;
    std::vector<glm::vec3> geometry;
    std::vector<SpawnPoint> spawn_points;

    Resref on_entered;
    Resref on_exhausted;
    Resref on_exit;
    Resref on_heartbeat;
    Resref on_userdefined;

    int32_t creatures_max = ~0;
    int32_t creatures_recommended = 0;
    int32_t difficulty = 0;
    int32_t difficulty_index = 0;
    int32_t reset_time = 0;
    int32_t respawns = 0;
    int32_t spawn_option = 0;

    uint8_t active = true;
    uint8_t player_only = false;
    uint8_t reset = true;
};

} // namespace nw
