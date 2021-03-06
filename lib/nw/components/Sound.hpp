#pragma once

#include "../serialization/Archives.hpp"
#include "Common.hpp"
#include "ObjectBase.hpp"

#include <flecs/flecs.h>

namespace nw {

struct Sound {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::sound;

    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const flecs::entity ent, SerializationProfile profile);
    static void serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile);

    std::vector<Resref> sounds;

    float distance_min = 0.0f;
    float distance_max = 0.0f;
    float elevation = 0.0f;
    uint32_t generated_type = 0; // Instance only
    uint32_t hours = 0;
    uint32_t interval = 0;
    uint32_t interval_variation = 0;
    float pitch_variation = 0.0f;
    float random_x = 0.0f;
    float random_y = 0.0f;

    bool active = 0;
    bool continuous = 0;
    bool looping = 0;
    bool positional = 0;
    uint8_t priority = 0;
    bool random = 0;
    bool random_position = 0;
    uint8_t times = 3; // Always
    uint8_t volume = 100;
    uint8_t volume_variation = 0;
};

} // namespace nw
