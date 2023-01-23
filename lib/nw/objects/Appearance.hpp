#pragma once

#include "../serialization/Archives.hpp"

namespace nw {

struct BodyParts {
    uint8_t belt = 0;
    uint8_t bicep_left = 0;
    uint8_t bicep_right = 0;
    uint8_t foot_left = 0;
    uint8_t foot_right = 0;
    uint8_t forearm_left = 0;
    uint8_t forearm_right = 0;
    uint8_t hand_left = 0;
    uint8_t hand_right = 0;
    uint8_t head = 0;
    uint8_t neck = 0;
    uint8_t pelvis = 0;
    uint8_t shin_left = 0;
    uint8_t shin_right = 0;
    uint8_t shoulder_left = 0;
    uint8_t shoulder_right = 0;
    uint8_t thigh_left = 0;
    uint8_t thigh_right = 0;
    uint8_t torso = 0;
};

struct Appearance {
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    int32_t phenotype = 0;
    uint32_t tail = 0;
    uint32_t wings = 0;

    uint16_t id = 0;
    uint16_t portrait_id;

    BodyParts body_parts;
    uint8_t hair = 0;
    uint8_t skin = 0;
    uint8_t tattoo1 = 0;
    uint8_t tattoo2 = 0;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Appearance& self, const GffStruct& archive);
bool serialize(const Appearance& self, GffBuilderStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
