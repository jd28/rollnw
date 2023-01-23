#pragma once

#include "../serialization/Archives.hpp"
#include "Common.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Sound : public ObjectBase {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::sound;
    static constexpr ResourceType::type restype = ResourceType::uts;

    Sound();

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Sound* as_sound() override { return this; }
    virtual const Sound* as_sound() const override { return this; }
    virtual bool instantiate() override { return true; }

    static bool deserialize(Sound* obj, const nlohmann::json& archive, SerializationProfile profile);
    static void serialize(const Sound* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
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

    bool instantiated_ = false;
};

// == Sound - Serialization - Gff =============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Sound* obj, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Sound* obj, GffBuilderStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Sound* obj, SerializationProfile profile);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
