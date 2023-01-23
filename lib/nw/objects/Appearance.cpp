#include "Appearance.hpp"

#include <nlohmann/json.hpp>

namespace nw {

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
        bp.at("belt").get_to(body_parts.belt);
        bp.at("bicep_left").get_to(body_parts.bicep_left);
        bp.at("bicep_right").get_to(body_parts.bicep_right);
        bp.at("foot_left").get_to(body_parts.foot_left);
        bp.at("foot_right").get_to(body_parts.foot_right);
        bp.at("forearm_left").get_to(body_parts.forearm_left);
        bp.at("forearm_right").get_to(body_parts.forearm_right);
        bp.at("hand_left").get_to(body_parts.hand_left);
        bp.at("hand_right").get_to(body_parts.hand_right);
        bp.at("head").get_to(body_parts.head);
        bp.at("neck").get_to(body_parts.neck);
        bp.at("pelvis").get_to(body_parts.pelvis);
        bp.at("shin_left").get_to(body_parts.shin_left);
        bp.at("shin_right").get_to(body_parts.shin_right);
        bp.at("shoulder_left").get_to(body_parts.shoulder_left);
        bp.at("shoulder_right").get_to(body_parts.shoulder_right);
        bp.at("thigh_left").get_to(body_parts.thigh_left);
        bp.at("thigh_right").get_to(body_parts.thigh_right);
        bp.at("torso").get_to(body_parts.torso);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
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
        {"bicep_left", body_parts.bicep_left},
        {"foot_left", body_parts.foot_left},
        {"forearm_left", body_parts.forearm_left},
        {"hand_left", body_parts.hand_left},
        {"shin_left", body_parts.shin_left},
        {"shoulder_left", body_parts.shoulder_left},
        {"thigh_left", body_parts.thigh_left},
        {"neck", body_parts.neck},
        {"pelvis", body_parts.pelvis},
        {"bicep_right", body_parts.bicep_right},
        {"foot_right", body_parts.foot_right},
        {"forearm_right", body_parts.forearm_right},
        {"hand_right", body_parts.hand_right},
        {"shin_right", body_parts.shin_right},
        {"shoulder_right", body_parts.shoulder_right},
        {"thigh_right", body_parts.thigh_right},
        {"torso", body_parts.torso},
    };

    return j;
}

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Appearance& self, const GffStruct& archive)
{
    if (!archive.get_to("Tail_New", self.tail, false)) {
        uint8_t old = 0;
        archive.get_to("Tail", old, false);
        self.tail = old;
    }
    if (!archive.get_to("Wings_New", self.wings, false)) {
        uint8_t old = 0;
        archive.get_to("Wings", old, false);
        self.wings = old;
    }

    archive.get_to("Appearance_Type", self.id);
    archive.get_to("PortraitId", self.portrait_id);
    archive.get_to("Appearance_Head", self.body_parts.head, false); // Only in dynamic models

    archive.get_to("ArmorPart_RFoot", self.body_parts.foot_right, false);
    archive.get_to("BodyPart_Belt", self.body_parts.belt, false);
    archive.get_to("BodyPart_LBicep", self.body_parts.bicep_left, false);
    archive.get_to("BodyPart_LFArm", self.body_parts.forearm_left, false);
    archive.get_to("BodyPart_LFoot", self.body_parts.foot_left, false);
    archive.get_to("BodyPart_LHand", self.body_parts.hand_left, false);
    archive.get_to("BodyPart_LShin", self.body_parts.shin_left, false);
    archive.get_to("BodyPart_LShoul", self.body_parts.shoulder_left, false);
    archive.get_to("BodyPart_LThigh", self.body_parts.thigh_left, false);
    archive.get_to("BodyPart_Neck", self.body_parts.neck, false);
    archive.get_to("BodyPart_Pelvis", self.body_parts.pelvis, false);
    archive.get_to("BodyPart_RBicep", self.body_parts.bicep_right, false);
    archive.get_to("BodyPart_RFArm", self.body_parts.forearm_right, false);
    archive.get_to("BodyPart_RHand", self.body_parts.hand_right, false);
    archive.get_to("BodyPart_RShin", self.body_parts.shin_right, false);
    archive.get_to("BodyPart_RShoul", self.body_parts.shoulder_right, false);
    archive.get_to("BodyPart_RThigh", self.body_parts.thigh_right, false);
    archive.get_to("BodyPart_Torso", self.body_parts.torso, false);

    archive.get_to("Color_Hair", self.hair, false);
    archive.get_to("Color_Skin", self.skin, false);
    archive.get_to("Color_Tattoo1", self.tattoo1, false);
    archive.get_to("Color_Tattoo2", self.tattoo2, false);
    archive.get_to("Phenotype", self.phenotype);

    return true;
}

bool serialize(const Appearance& self, GffBuilderStruct& archive)
{
    archive.add_field("Tail_New", self.tail)
        .add_field("Wings_New", self.wings)
        .add_field("Appearance_Type", self.id)
        .add_field("PortraitId", self.portrait_id)
        .add_field("Appearance_Head", self.body_parts.head)
        .add_field("BodyPart_Belt", self.body_parts.belt)
        .add_field("BodyPart_LBicep", self.body_parts.bicep_left)
        .add_field("BodyPart_LFArm", self.body_parts.forearm_left)
        .add_field("BodyPart_LFoot", self.body_parts.foot_left)
        .add_field("BodyPart_LHand", self.body_parts.hand_left)
        .add_field("BodyPart_LShin", self.body_parts.shin_left)
        .add_field("BodyPart_LShoul", self.body_parts.shoulder_left)
        .add_field("BodyPart_LThigh", self.body_parts.thigh_left)
        .add_field("BodyPart_Neck", self.body_parts.neck)
        .add_field("BodyPart_Pelvis", self.body_parts.pelvis)
        .add_field("BodyPart_RBicep", self.body_parts.bicep_right)
        .add_field("BodyPart_RFArm", self.body_parts.forearm_right)
        .add_field("ArmorPart_RFoot", self.body_parts.foot_right)
        .add_field("BodyPart_RHand", self.body_parts.hand_right)
        .add_field("BodyPart_RShin", self.body_parts.shin_right)
        .add_field("BodyPart_RShoul", self.body_parts.shoulder_right)
        .add_field("BodyPart_RThigh", self.body_parts.thigh_right)
        .add_field("BodyPart_Torso", self.body_parts.torso)
        .add_field("Color_Hair", self.hair)
        .add_field("Color_Skin", self.skin)
        .add_field("Color_Tattoo1", self.tattoo1)
        .add_field("Color_Tattoo2", self.tattoo2)
        .add_field("Phenotype", self.phenotype);

    return true;
}
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
