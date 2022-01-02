#include "Appearance.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Appearance::from_gff(const GffInputArchiveStruct& archive)
{
    if (!archive.get_to("Tail_New", tail, false)) {
        uint8_t old = 0;
        archive.get_to("Tail", old, false);
        tail = old;
    }
    if (!archive.get_to("Wings_New", wings, false)) {
        uint8_t old = 0;
        archive.get_to("Wings", old, false);
        wings = old;
    }

    archive.get_to("Appearance_Type", id);
    archive.get_to("PortraitId", portrait_id);
    archive.get_to("Appearance_Head", body_parts.head);
    archive.get_to("BodyPart_Belt", body_parts.belt, false);
    archive.get_to("BodyPart_LBicep", body_parts.left_bicep, false);
    archive.get_to("BodyPart_LFArm", body_parts.left_forearm, false);
    archive.get_to("BodyPart_LFoot", body_parts.left_foot, false);
    archive.get_to("BodyPart_LHand", body_parts.left_hand, false);
    archive.get_to("BodyPart_LShin", body_parts.left_shin, false);
    archive.get_to("BodyPart_LShoul", body_parts.left_shoulder, false);
    archive.get_to("BodyPart_LThigh", body_parts.left_thigh, false);
    archive.get_to("BodyPart_Neck", body_parts.neck, false);
    archive.get_to("BodyPart_Pelvis", body_parts.pelvis, false);
    archive.get_to("BodyPart_RBicep", body_parts.right_bicep, false);
    archive.get_to("BodyPart_RFArm", body_parts.right_forearm, false);
    archive.get_to("ArmorPart_RFoot", body_parts.right_foot, false);
    archive.get_to("BodyPart_RHand", body_parts.right_hand, false);
    archive.get_to("BodyPart_RShin", body_parts.right_shin, false);
    archive.get_to("BodyPart_RShoul", body_parts.right_shoulder, false);
    archive.get_to("BodyPart_RThigh", body_parts.right_thigh, false);
    archive.get_to("BodyPart_Torso", body_parts.torso, false);
    archive.get_to("Color_Hair", hair, false);
    archive.get_to("Color_Skin", skin, false);
    archive.get_to("Color_Tattoo1", tattoo1, false);
    archive.get_to("Color_Tattoo2", tattoo2, false);
    archive.get_to("Phenotype", phenotype);

    return true;
}

bool Appearance::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("phenotype").get_to(phenotype);
        archive.at("tail").get_to(tail);
        archive.at("wings").get_to(wings);
        archive.at("id").get_to(id);
        archive.at("portrait_id").get_to(portrait_id);
        archive.at("hair").get_to(hair);
        archive.at("skin").get_to(skin);
        archive.at("tattoo1").get_to(tattoo1);
        archive.at("tattoo2").get_to(tattoo2);

        const auto& bp = archive.at("body_parts");
        bp.at("head").get_to(body_parts.head);
        bp.at("belt").get_to(body_parts.belt);
        bp.at("left_bicep").get_to(body_parts.left_bicep);
        bp.at("left_foot").get_to(body_parts.left_foot);
        bp.at("left_forearm").get_to(body_parts.left_forearm);
        bp.at("left_hand").get_to(body_parts.left_hand);
        bp.at("left_shin").get_to(body_parts.left_shin);
        bp.at("left_shoulder").get_to(body_parts.left_shoulder);
        bp.at("left_thigh").get_to(body_parts.left_thigh);
        bp.at("neck").get_to(body_parts.neck);
        bp.at("pelvis").get_to(body_parts.pelvis);
        bp.at("right_bicep").get_to(body_parts.right_bicep);
        bp.at("right_foot").get_to(body_parts.right_foot);
        bp.at("right_forearm").get_to(body_parts.right_forearm);
        bp.at("right_hand").get_to(body_parts.right_hand);
        bp.at("right_shin").get_to(body_parts.right_shin);
        bp.at("right_shoulder").get_to(body_parts.right_shoulder);
        bp.at("right_thigh").get_to(body_parts.right_thigh);
        bp.at("torso").get_to(body_parts.torso);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool Appearance::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_fields({
        {"Tail_New", tail},
        {"Wings_New", wings},
        {"Appearance_Type", id},
        {"PortraitId", portrait_id},
        {"Appearance_Head", body_parts.head},
        {"BodyPart_Belt", body_parts.belt},
        {"BodyPart_LBicep", body_parts.left_bicep},
        {"BodyPart_LFArm", body_parts.left_forearm},
        {"BodyPart_LFoot", body_parts.left_foot},
        {"BodyPart_LHand", body_parts.left_hand},
        {"BodyPart_LShin", body_parts.left_shin},
        {"BodyPart_LShoul", body_parts.left_shoulder},
        {"BodyPart_LThigh", body_parts.left_thigh},
        {"BodyPart_Neck", body_parts.neck},
        {"BodyPart_Pelvis", body_parts.pelvis},
        {"BodyPart_RBicep", body_parts.right_bicep},
        {"BodyPart_RFArm", body_parts.right_forearm},
        {"ArmorPart_RFoot", body_parts.right_foot},
        {"BodyPart_RHand", body_parts.right_hand},
        {"BodyPart_RShin", body_parts.right_shin},
        {"BodyPart_RShoul", body_parts.right_shoulder},
        {"BodyPart_RThigh", body_parts.right_thigh},
        {"BodyPart_Torso", body_parts.torso},
        {"Color_Hair", hair},
        {"Color_Skin", skin},
        {"Color_Tattoo1", tattoo1},
        {"Color_Tattoo2", tattoo2},
        {"Phenotype", phenotype},
    });

    return true;
}

nlohmann::json Appearance::to_json() const
{
    nlohmann::json j;

    j["phenotype"] = phenotype;
    j["tail"] = tail;
    j["wings"] = wings;
    j["id"] = id;
    j["portrait_id"] = portrait_id;
    j["hair"] = hair;
    j["skin"] = skin;
    j["tattoo1"] = tattoo1;
    j["tattoo2"] = tattoo2;
    j["body_parts"] = {
        {"head", body_parts.head},
        {"belt", body_parts.belt},
        {"left_bicep", body_parts.left_bicep},
        {"left_foot", body_parts.left_foot},
        {"left_forearm", body_parts.left_forearm},
        {"left_hand", body_parts.left_hand},
        {"left_shin", body_parts.left_shin},
        {"left_shoulder", body_parts.left_shoulder},
        {"left_thigh", body_parts.left_thigh},
        {"neck", body_parts.neck},
        {"pelvis", body_parts.pelvis},
        {"right_bicep", body_parts.right_bicep},
        {"right_foot", body_parts.right_foot},
        {"right_forearm", body_parts.right_forearm},
        {"right_hand", body_parts.right_hand},
        {"right_shin", body_parts.right_shin},
        {"right_shoulder", body_parts.right_shoulder},
        {"right_thigh", body_parts.right_thigh},
        {"torso", body_parts.torso},
    };

    return j;
}

} // namespace nw
