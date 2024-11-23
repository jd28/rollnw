#include "Appearance.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

CreatureAppearance::CreatureAppearance()
{
    colors.fill(0);
}

bool CreatureAppearance::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("phenotype").get_to(phenotype);
        archive.at("tail").get_to(tail);
        archive.at("wings").get_to(wings);
        archive.at("id").get_to(id);

        auto it = archive.find("portrait");
        if (it != archive.end()) {
            it->get_to(portrait);
        }

        archive.at("portrait_id").get_to(portrait_id);
        archive.at("hair").get_to(colors[CreatureColors::hair]);
        archive.at("skin").get_to(colors[CreatureColors::skin]);
        archive.at("tattoo1").get_to(colors[CreatureColors::tatoo1]);
        archive.at("tattoo2").get_to(colors[CreatureColors::tatoo2]);

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

nlohmann::json CreatureAppearance::to_json() const
{
    nlohmann::json j;

    j["phenotype"] = phenotype;
    j["tail"] = tail;
    j["wings"] = wings;
    j["id"] = id;

    if (portrait.length() > 0) {
        j["portrait"] = portrait;
    }

    j["portrait_id"] = portrait_id;
    j["hair"] = colors[CreatureColors::hair];
    j["skin"] = colors[CreatureColors::skin];
    j["tattoo1"] = colors[CreatureColors::tatoo1];
    j["tattoo2"] = colors[CreatureColors::tatoo2];
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

bool deserialize(CreatureAppearance& self, const GffStruct& archive)
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
    archive.get_to("Portrait", self.portrait, false);
    archive.get_to("PortraitId", self.portrait_id, self.portrait.length() == 0);
    archive.get_to("Appearance_Head", self.body_parts.head, false); // Only in dynamic models

    if (!archive.get_to("xArmorPart_RFoot", self.body_parts.foot_right, false)) {
        archive.get_to("ArmorPart_RFoot", self.body_parts.foot_right, false);
    }
    if (!archive.get_to("xBodyPart_Belt", self.body_parts.belt, false)) {
        archive.get_to("BodyPart_Belt", self.body_parts.belt, false);
    }
    if (!archive.get_to("xBodyPart_LBicep", self.body_parts.bicep_left, false)) {
        archive.get_to("BodyPart_LBicep", self.body_parts.bicep_left, false);
    }
    if (!archive.get_to("xBodyPart_LFArm", self.body_parts.forearm_left, false)) {
        archive.get_to("BodyPart_LFArm", self.body_parts.forearm_left, false);
    }
    if (!archive.get_to("xBodyPart_LFoot", self.body_parts.foot_left, false)) {
        archive.get_to("BodyPart_LFoot", self.body_parts.foot_left, false);
    }
    if (!archive.get_to("xBodyPart_LHand", self.body_parts.hand_left, false)) {
        archive.get_to("BodyPart_LHand", self.body_parts.hand_left, false);
    }
    if (!archive.get_to("xBodyPart_LShin", self.body_parts.shin_left, false)) {
        archive.get_to("BodyPart_LShin", self.body_parts.shin_left, false);
    }
    if (!archive.get_to("xBodyPart_LShoul", self.body_parts.shoulder_left, false)) {
        archive.get_to("BodyPart_LShoul", self.body_parts.shoulder_left, false);
    }
    if (!archive.get_to("xBodyPart_LThigh", self.body_parts.thigh_left, false)) {
        archive.get_to("BodyPart_LThigh", self.body_parts.thigh_left, false);
    }
    if (!archive.get_to("xBodyPart_Neck", self.body_parts.neck, false)) {
        archive.get_to("BodyPart_Neck", self.body_parts.neck, false);
    }
    if (!archive.get_to("xBodyPart_Pelvis", self.body_parts.pelvis, false)) {
        archive.get_to("BodyPart_Pelvis", self.body_parts.pelvis, false);
    }
    if (!archive.get_to("xBodyPart_RBicep", self.body_parts.bicep_right, false)) {
        archive.get_to("BodyPart_RBicep", self.body_parts.bicep_right, false);
    }
    if (!archive.get_to("xBodyPart_RFArm", self.body_parts.forearm_right, false)) {
        archive.get_to("BodyPart_RFArm", self.body_parts.forearm_right, false);
    }
    if (!archive.get_to("xBodyPart_RHand", self.body_parts.hand_right, false)) {
        archive.get_to("BodyPart_RHand", self.body_parts.hand_right, false);
    }
    if (!archive.get_to("xBodyPart_RShin", self.body_parts.shin_right, false)) {
        archive.get_to("BodyPart_RShin", self.body_parts.shin_right, false);
    }
    if (!archive.get_to("xBodyPart_RShoul", self.body_parts.shoulder_right, false)) {
        archive.get_to("BodyPart_RShoul", self.body_parts.shoulder_right, false);
    }
    if (!archive.get_to("xBodyPart_RThigh", self.body_parts.thigh_right, false)) {
        archive.get_to("BodyPart_RThigh", self.body_parts.thigh_right, false);
    }
    if (!archive.get_to("xBodyPart_Torso", self.body_parts.torso, false)) {
        archive.get_to("BodyPart_Torso", self.body_parts.torso, false);
    }

    archive.get_to("Color_Hair", self.colors[CreatureColors::hair], false);
    archive.get_to("Color_Skin", self.colors[CreatureColors::skin], false);
    archive.get_to("Color_Tattoo1", self.colors[CreatureColors::tatoo1], false);
    archive.get_to("Color_Tattoo2", self.colors[CreatureColors::tatoo2], false);
    archive.get_to("Phenotype", self.phenotype);

    return true;
}

bool serialize(const CreatureAppearance& self, GffBuilderStruct& archive)
{
    archive.add_field("Tail_New", self.tail)
        .add_field("Wings_New", self.wings)
        .add_field("Appearance_Type", self.id)
        .add_field("PortraitId", self.portrait_id)
        .add_field("Appearance_Head", uint8_t(self.body_parts.head))
        .add_field("BodyPart_Belt", uint8_t(self.body_parts.belt))
        .add_field("BodyPart_LBicep", uint8_t(self.body_parts.bicep_left))
        .add_field("BodyPart_LFArm", uint8_t(self.body_parts.forearm_left))
        .add_field("BodyPart_LFoot", uint8_t(self.body_parts.foot_left))
        .add_field("BodyPart_LHand", uint8_t(self.body_parts.hand_left))
        .add_field("BodyPart_LShin", uint8_t(self.body_parts.shin_left))
        .add_field("BodyPart_LShoul", uint8_t(self.body_parts.shoulder_left))
        .add_field("BodyPart_LThigh", uint8_t(self.body_parts.thigh_left))
        .add_field("BodyPart_Neck", uint8_t(self.body_parts.neck))
        .add_field("BodyPart_Pelvis", uint8_t(self.body_parts.pelvis))
        .add_field("BodyPart_RBicep", uint8_t(self.body_parts.bicep_right))
        .add_field("BodyPart_RFArm", uint8_t(self.body_parts.forearm_right))
        .add_field("ArmorPart_RFoot", uint8_t(self.body_parts.foot_right))
        .add_field("BodyPart_RHand", uint8_t(self.body_parts.hand_right))
        .add_field("BodyPart_RShin", uint8_t(self.body_parts.shin_right))
        .add_field("BodyPart_RShoul", uint8_t(self.body_parts.shoulder_right))
        .add_field("BodyPart_RThigh", uint8_t(self.body_parts.thigh_right))
        .add_field("BodyPart_Torso", uint8_t(self.body_parts.torso))
        .add_field("xAppearance_Head", self.body_parts.head)
        .add_field("xBodyPart_Belt", self.body_parts.belt)
        .add_field("xBodyPart_LBicep", self.body_parts.bicep_left)
        .add_field("xBodyPart_LFArm", self.body_parts.forearm_left)
        .add_field("xBodyPart_LFoot", self.body_parts.foot_left)
        .add_field("xBodyPart_LHand", self.body_parts.hand_left)
        .add_field("xBodyPart_LShin", self.body_parts.shin_left)
        .add_field("xBodyPart_LShoul", self.body_parts.shoulder_left)
        .add_field("xBodyPart_LThigh", self.body_parts.thigh_left)
        .add_field("xBodyPart_Neck", self.body_parts.neck)
        .add_field("xBodyPart_Pelvis", self.body_parts.pelvis)
        .add_field("xBodyPart_RBicep", self.body_parts.bicep_right)
        .add_field("xBodyPart_RFArm", self.body_parts.forearm_right)
        .add_field("xArmorPart_RFoot", self.body_parts.foot_right)
        .add_field("xBodyPart_RHand", self.body_parts.hand_right)
        .add_field("xBodyPart_RShin", self.body_parts.shin_right)
        .add_field("xBodyPart_RShoul", self.body_parts.shoulder_right)
        .add_field("xBodyPart_RThigh", self.body_parts.thigh_right)
        .add_field("xBodyPart_Torso", self.body_parts.torso)
        .add_field("Color_Hair", self.colors[CreatureColors::hair])
        .add_field("Color_Skin", self.colors[CreatureColors::skin])
        .add_field("Color_Tattoo1", self.colors[CreatureColors::tatoo1])
        .add_field("Color_Tattoo2", self.colors[CreatureColors::tatoo2])
        .add_field("Phenotype", self.phenotype);

    if (self.portrait.length() > 0) {
        archive.add_field("Portrait", self.portrait);
    }

    return true;
}

} // namespace nw
