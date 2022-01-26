#pragma once

#include "../serialization/Archives.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"

namespace nw {

struct Sound : public ObjectBase {
    Sound(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Sound(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;

    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Sound* as_sound() override { return this; }
    virtual const Sound* as_sound() const override { return this; }

    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    GffOutputArchive to_gff(SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    float distance_min = 0.0f;
    float distance_max = 0.0f;
    float elevation = 0.0f;
    uint32_t hours = 0;
    uint32_t interval = 0;
    uint32_t interval_variation = 0;
    float pitch_variation = 0.0f;
    float random_x = 0.0f;
    float random_y = 0.0f;
    std::vector<Resref> sounds;

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

    // Instance only
    uint8_t generated_type = 0;
};

} // namespace nw
