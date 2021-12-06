#pragma once

#include <cstdint>

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

struct CreatureAppearance {
    CreatureAppearance() = default;

    template <typename InputArchiveStruct>
    bool deserialize(const InputArchiveStruct& str);

    int32_t phenotype = 0;
    uint32_t tail = 0;
    uint32_t wings = 0;

    uint16_t id = 0;
    uint16_t portrait_id = 0;

    BodyParts body_parts;
    uint8_t hair = 0;
    uint8_t skin = 0;
    uint8_t tattoo1 = 0;
    uint8_t tattoo2 = 0;
};

template <typename InputArchiveStruct>
bool CreatureAppearance::deserialize(const InputArchiveStruct& str)
{
    if (!str.get_to("Tail_New", tail, false)) {
        uint8_t old = 0;
        str.get_to("Tail", old, false);
        tail = old;
    }
    if (!str.get_to("Wings_New", wings, false)) {
        uint8_t old = 0;
        str.get_to("Wings", old, false);
        wings = old;
    }

    str.get_to("Appearance_Type", id);
    str.get_to("PortraitId", portrait_id);

    str.get_to("BodyPart_Belt", body_parts.belt, false);
    str.get_to("BodyPart_LBicep", body_parts.left_bicep, false);
    str.get_to("BodyPart_LFArm", body_parts.left_forearm, false);
    str.get_to("BodyPart_LFoot", body_parts.left_foot, false);
    str.get_to("BodyPart_LHand", body_parts.left_hand, false);
    str.get_to("BodyPart_LShin", body_parts.left_shin, false);
    str.get_to("BodyPart_LShoul", body_parts.left_shoulder, false);
    str.get_to("BodyPart_LThigh", body_parts.left_thigh, false);
    str.get_to("BodyPart_Neck", body_parts.neck, false);
    str.get_to("BodyPart_Pelvis", body_parts.pelvis, false);
    str.get_to("BodyPart_RBicep", body_parts.right_bicep, false);
    str.get_to("BodyPart_RFArm", body_parts.right_forearm, false);
    str.get_to("ArmorPart_RFoot", body_parts.right_foot, false);
    str.get_to("BodyPart_RHand", body_parts.right_hand, false);
    str.get_to("BodyPart_RShin", body_parts.right_shin, false);
    str.get_to("BodyPart_RShoul", body_parts.right_shoulder, false);
    str.get_to("BodyPart_RThigh", body_parts.right_thigh, false);
    str.get_to("BodyPart_Torso", body_parts.torso, false);
    str.get_to("Color_Hair", hair, false);
    str.get_to("Color_Skin", skin, false);
    str.get_to("Color_Tattoo1", tattoo1, false);
    str.get_to("Color_Tattoo2", tattoo2, false);
    str.get_to("Phenotype", phenotype);

    return true;
}

} // namespace nw
