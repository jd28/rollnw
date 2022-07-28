#pragma once

#include "../serialization/Archives.hpp"
#include "Common.hpp"
#include "ObjectBase.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace nw {

struct EncounterScripts {
    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    Resref on_entered;
    Resref on_exhausted;
    Resref on_exit;
    Resref on_heartbeat;
    Resref on_user_defined;
};

struct SpawnCreature {
    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    int32_t appearance;
    float cr;
    Resref resref;
    bool single_spawn;
};

struct SpawnPoint {
    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    float orientation; // In rad
    glm::vec3 position;
};

struct Encounter : public ObjectBase {
    Encounter();

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::encounter;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Encounter* as_encounter() override { return this; }
    virtual const Encounter* as_encounter() const override { return this; }

    static bool deserialize(Encounter* obj, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(Encounter* obj, const nlohmann::json& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const Encounter* obj, SerializationProfile profile);
    static bool serialize(const Encounter* obj, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static bool serialize(const Encounter* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
    EncounterScripts scripts;
    std::vector<SpawnCreature> creatures;
    std::vector<glm::vec3> geometry;
    std::vector<SpawnPoint> spawn_points;

    int32_t creatures_max = -1;
    int32_t creatures_recommended = 0;
    int32_t difficulty = 0;
    int32_t difficulty_index = 0;
    uint32_t faction = 0;
    int32_t reset_time = 0;
    int32_t respawns = 0;
    int32_t spawn_option = 0;

    bool active = true;
    bool player_only = false;
    bool reset = true;
};

} // namespace nw
