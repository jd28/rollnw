#pragma once

#include "../../serialization/Serialization.hpp"

namespace nw {

struct BodyParts {
    uint8_t belt = 0;
    uint8_t left_bicep = 0;
    uint8_t left_foot = 0;
    uint8_t left_forearm = 0;
    uint8_t left_hand = 0;
    uint8_t left_shin = 0;
    uint8_t left_shoulder = 0;
    uint8_t left_thigh = 0;
    uint8_t neck = 0;
    uint8_t pelvis = 0;
    uint8_t right_bicep = 0;
    uint8_t right_foot = 0;
    uint8_t right_forearm = 0;
    uint8_t right_hand = 0;
    uint8_t right_shin = 0;
    uint8_t right_shoulder = 0;
    uint8_t right_thigh = 0;
    uint8_t torso = 0;
};

struct Appearance {
    Appearance(const GffInputArchiveStruct gff);

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

} // namespace nw
