#include "Appearance.hpp"

namespace nw {

Appearance::Appearance(const GffStruct gff)
{

    if (!gff.get_to("Tail_New", tail, false)) {
        uint8_t old = 0;
        gff.get_to("Tail", old, false);
        tail = old;
    }
    if (!gff.get_to("Wings_New", wings, false)) {
        uint8_t old = 0;
        gff.get_to("Wings", old, false);
        wings = old;
    }

    gff.get_to("Appearance_Type", id);
    gff.get_to("PortraitId", portrait_id);

    gff.get_to("BodyPart_Belt", body_parts.belt, false);
    gff.get_to("BodyPart_LBicep", body_parts.left_bicep, false);
    gff.get_to("BodyPart_LFArm", body_parts.left_forearm, false);
    gff.get_to("BodyPart_LFoot", body_parts.left_foot, false);
    gff.get_to("BodyPart_LHand", body_parts.left_hand, false);
    gff.get_to("BodyPart_LShin", body_parts.left_shin, false);
    gff.get_to("BodyPart_LShoul", body_parts.left_shoulder, false);
    gff.get_to("BodyPart_LThigh", body_parts.left_thigh, false);
    gff.get_to("BodyPart_Neck", body_parts.neck, false);
    gff.get_to("BodyPart_Pelvis", body_parts.pelvis, false);
    gff.get_to("BodyPart_RBicep", body_parts.right_bicep, false);
    gff.get_to("BodyPart_RFArm", body_parts.right_forearm, false);
    gff.get_to("ArmorPart_RFoot", body_parts.right_foot, false);
    gff.get_to("BodyPart_RHand", body_parts.right_hand, false);
    gff.get_to("BodyPart_RShin", body_parts.right_shin, false);
    gff.get_to("BodyPart_RShoul", body_parts.right_shoulder, false);
    gff.get_to("BodyPart_RThigh", body_parts.right_thigh, false);
    gff.get_to("BodyPart_Torso", body_parts.torso, false);
    gff.get_to("Color_Hair", hair, false);
    gff.get_to("Color_Skin", skin, false);
    gff.get_to("Color_Tattoo1", tattoo1, false);
    gff.get_to("Color_Tattoo2", tattoo2, false);
    gff.get_to("Phenotype", phenotype);
}

} // namespace nw
