#pragma once

#include "../serialization/Serialization.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"

namespace nw {

struct Sound : public ObjectBase {
    Sound(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() override { return &common_; }

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

    uint8_t active = 0;
    uint8_t continuous = 0;
    uint8_t looping = 0;
    uint8_t positional = 0;
    uint8_t priority = 0;
    uint8_t random = 0;
    uint8_t random_position = 0;
    uint8_t times = 3; // Always
    uint8_t volume = 100;
    uint8_t volume_variation = 0;

    // Instance only
    uint8_t generated_type = 0;
};

} // namespace nw
