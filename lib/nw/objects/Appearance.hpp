#pragma once

#include "../serialization/Serialization.hpp"

namespace nw {

struct CreatureColors {
    enum type : uint8_t {
        hair = 0,
        skin = 1,
        tatoo1 = 2,
        tatoo2 = 3,
    };
};

struct BodyParts {
    uint16_t belt = 0;
    uint16_t bicep_left = 0;
    uint16_t bicep_right = 0;
    uint16_t foot_left = 0;
    uint16_t foot_right = 0;
    uint16_t forearm_left = 0;
    uint16_t forearm_right = 0;
    uint16_t hand_left = 0;
    uint16_t hand_right = 0;
    uint16_t head = 0;
    uint16_t neck = 0;
    uint16_t pelvis = 0;
    uint16_t shin_left = 0;
    uint16_t shin_right = 0;
    uint16_t shoulder_left = 0;
    uint16_t shoulder_right = 0;
    uint16_t thigh_left = 0;
    uint16_t thigh_right = 0;
    uint16_t torso = 0;
};

struct CreatureAppearance {
    CreatureAppearance();

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    int32_t phenotype = 0;
    uint32_t tail = 0;
    uint32_t wings = 0;

    uint16_t id = 0;
    Resref portrait;
    uint16_t portrait_id = 0;

    BodyParts body_parts;
    std::array<uint8_t, 4> colors;
};

bool deserialize(CreatureAppearance& self, const GffStruct& archive);
bool serialize(const CreatureAppearance& self, GffBuilderStruct& archive);

} // namespace nw
