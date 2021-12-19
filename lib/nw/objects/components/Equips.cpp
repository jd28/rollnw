#include "Equips.hpp"

#include "../Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Equips::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    size_t sz = archive["Equip_ItemList"].size();
    for (size_t i = 0; i < sz; ++i) {
        auto st = archive["Equip_ItemList"][i];
        switch (static_cast<EquipSlot>(st.id())) {
        default:
            LOG_F(ERROR, "equips invalid equipment slot: {}", st.id());
            break;
        case EquipSlot::head:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                head = r;
            } else {
                head = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::chest:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                chest = r;
            } else {
                chest = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::boots:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                boots = r;
            } else {
                boots = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::arms:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                arms = r;
            } else {
                arms = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::righthand:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                righthand = r;
            } else {
                righthand = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::lefthand:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                lefthand = r;
            } else {
                lefthand = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::cloak:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                cloak = r;
            } else {
                cloak = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::leftring:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                leftring = r;
            } else {
                leftring = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::rightring:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                rightring = r;
            } else {
                rightring = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::neck:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                neck = r;
            } else {
                neck = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::belt:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                belt = r;
            } else {
                belt = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::arrows:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                arrows = r;
            } else {
                arrows = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::bullets:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                bullets = r;
            } else {
                bullets = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::bolts:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                bolts = r;
            } else {
                bolts = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_left:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_left = r;
            } else {
                creature_left = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_right:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_right = r;
            } else {
                creature_right = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_bite:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_bite = r;
            } else {
                creature_bite = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_skin:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_skin = r;
            } else {
                creature_skin = std::make_unique<Item>(st, profile);
            }
            break;
        }
    }

    return true;
}

bool Equips::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    // TODO - Clean this up..
    try {
        if (profile == SerializationProfile::blueprint) {
            if (archive.find("head") != std::end(archive))
                head = archive.at("head").get<Resref>();
            if (archive.find("chest") != std::end(archive))
                chest = archive.at("chest").get<Resref>();
            if (archive.find("boots") != std::end(archive))
                boots = archive.at("boots").get<Resref>();
            if (archive.find("arms") != std::end(archive))
                arms = archive.at("arms").get<Resref>();
            if (archive.find("righthand") != std::end(archive))
                righthand = archive.at("righthand").get<Resref>();
            if (archive.find("lefthand") != std::end(archive))
                lefthand = archive.at("lefthand").get<Resref>();
            if (archive.find("cloak") != std::end(archive))
                cloak = archive.at("cloak").get<Resref>();
            if (archive.find("leftring") != std::end(archive))
                leftring = archive.at("leftring").get<Resref>();
            if (archive.find("rightring") != std::end(archive))
                rightring = archive.at("rightring").get<Resref>();
            if (archive.find("neck") != std::end(archive))
                neck = archive.at("neck").get<Resref>();
            if (archive.find("belt") != std::end(archive))
                belt = archive.at("belt").get<Resref>();
            if (archive.find("arrows") != std::end(archive))
                arrows = archive.at("arrows").get<Resref>();
            if (archive.find("bullets") != std::end(archive))
                bullets = archive.at("bullets").get<Resref>();
            if (archive.find("bolts") != std::end(archive))
                bolts = archive.at("bolts").get<Resref>();
            if (archive.find("creature_left") != std::end(archive))
                creature_left = archive.at("creature_left").get<Resref>();
            if (archive.find("creature_right") != std::end(archive))
                creature_right = archive.at("creature_right").get<Resref>();
            if (archive.find("creature_bite") != std::end(archive))
                creature_bite = archive.at("creature_bite").get<Resref>();
            if (archive.find("creature_skin") != std::end(archive))
                creature_skin = archive.at("creature_skin").get<Resref>();
        } else {
            if (archive.find("head") != std::end(archive))
                head = std::make_unique<Item>(archive.at("head"), profile);
            if (archive.find("chest") != std::end(archive))
                chest = std::make_unique<Item>(archive.at("chest"), profile);
            if (archive.find("boots") != std::end(archive))
                boots = std::make_unique<Item>(archive.at("boots"), profile);
            if (archive.find("arms") != std::end(archive))
                arms = std::make_unique<Item>(archive.at("arms"), profile);
            if (archive.find("righthand") != std::end(archive))
                righthand = std::make_unique<Item>(archive.at("righthand"), profile);
            if (archive.find("lefthand") != std::end(archive))
                lefthand = std::make_unique<Item>(archive.at("lefthand"), profile);
            if (archive.find("cloak") != std::end(archive))
                cloak = std::make_unique<Item>(archive.at("cloak"), profile);
            if (archive.find("leftring") != std::end(archive))
                leftring = std::make_unique<Item>(archive.at("leftring"), profile);
            if (archive.find("rightring") != std::end(archive))
                rightring = std::make_unique<Item>(archive.at("rightring"), profile);
            if (archive.find("neck") != std::end(archive))
                neck = std::make_unique<Item>(archive.at("neck"), profile);
            if (archive.find("belt") != std::end(archive))
                belt = std::make_unique<Item>(archive.at("belt"), profile);
            if (archive.find("arrows") != std::end(archive))
                arrows = std::make_unique<Item>(archive.at("arrows"), profile);
            if (archive.find("bullets") != std::end(archive))
                bullets = std::make_unique<Item>(archive.at("bullets"), profile);
            if (archive.find("bolts") != std::end(archive))
                bolts = std::make_unique<Item>(archive.at("bolts"), profile);
            if (archive.find("creature_left") != std::end(archive))
                creature_left = std::make_unique<Item>(archive.at("creature_left"), profile);
            if (archive.find("creature_right") != std::end(archive))
                creature_right = std::make_unique<Item>(archive.at("creature_right"), profile);
            if (archive.find("creature_bite") != std::end(archive))
                creature_bite = std::make_unique<Item>(archive.at("creature_bite"), profile);
            if (archive.find("creature_skin") != std::end(archive))
                creature_skin = std::make_unique<Item>(archive.at("creature_skin"), profile);
        }
    } catch (const nlohmann::json::exception& e) {
        return false;
    }
    return true;
}

nlohmann::json Equips::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    if (profile == SerializationProfile::blueprint) {
        if (std::holds_alternative<Resref>(head))
            j["head"] = std::get<Resref>(head);
        if (std::holds_alternative<Resref>(chest))
            j["chest"] = std::get<Resref>(chest);
        if (std::holds_alternative<Resref>(boots))
            j["boots"] = std::get<Resref>(boots);
        if (std::holds_alternative<Resref>(arms))
            j["arms"] = std::get<Resref>(arms);
        if (std::holds_alternative<Resref>(righthand))
            j["righthand"] = std::get<Resref>(righthand);
        if (std::holds_alternative<Resref>(lefthand))
            j["lefthand"] = std::get<Resref>(lefthand);
        if (std::holds_alternative<Resref>(cloak))
            j["cloak"] = std::get<Resref>(cloak);
        if (std::holds_alternative<Resref>(leftring))
            j["leftring"] = std::get<Resref>(leftring);
        if (std::holds_alternative<Resref>(rightring))
            j["rightring"] = std::get<Resref>(rightring);
        if (std::holds_alternative<Resref>(neck))
            j["neck"] = std::get<Resref>(neck);
        if (std::holds_alternative<Resref>(belt))
            j["belt"] = std::get<Resref>(belt);
        if (std::holds_alternative<Resref>(arrows))
            j["arrows"] = std::get<Resref>(arrows);
        if (std::holds_alternative<Resref>(bullets))
            j["bullets"] = std::get<Resref>(bullets);
        if (std::holds_alternative<Resref>(bolts))
            j["bolts"] = std::get<Resref>(bolts);
        if (std::holds_alternative<Resref>(creature_left))
            j["creature_left"] = std::get<Resref>(creature_left);
        if (std::holds_alternative<Resref>(creature_right))
            j["creature_right"] = std::get<Resref>(creature_right);
        if (std::holds_alternative<Resref>(creature_bite))
            j["creature_bite"] = std::get<Resref>(creature_bite);
        if (std::holds_alternative<Resref>(creature_skin))
            j["creature_skin"] = std::get<Resref>(creature_skin);
    } else {
        if (std::holds_alternative<std::unique_ptr<Item>>(head))
            j["head"] = std::get<std::unique_ptr<Item>>(head)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(chest))
            j["chest"] = std::get<std::unique_ptr<Item>>(chest)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(boots))
            j["boots"] = std::get<std::unique_ptr<Item>>(boots)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(arms))
            j["arms"] = std::get<std::unique_ptr<Item>>(arms)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(righthand))
            j["righthand"] = std::get<std::unique_ptr<Item>>(righthand)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(lefthand))
            j["lefthand"] = std::get<std::unique_ptr<Item>>(lefthand)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(cloak))
            j["cloak"] = std::get<std::unique_ptr<Item>>(cloak)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(leftring))
            j["leftring"] = std::get<std::unique_ptr<Item>>(leftring)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(rightring))
            j["rightring"] = std::get<std::unique_ptr<Item>>(rightring)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(neck))
            j["neck"] = std::get<std::unique_ptr<Item>>(neck)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(belt))
            j["belt"] = std::get<std::unique_ptr<Item>>(belt)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(arrows))
            j["arrows"] = std::get<std::unique_ptr<Item>>(arrows)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(bullets))
            j["bullets"] = std::get<std::unique_ptr<Item>>(bullets)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(bolts))
            j["bolts"] = std::get<std::unique_ptr<Item>>(bolts)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(creature_left))
            j["creature_left"] = std::get<std::unique_ptr<Item>>(creature_left)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(creature_right))
            j["creature_right"] = std::get<std::unique_ptr<Item>>(creature_right)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(creature_bite))
            j["creature_bite"] = std::get<std::unique_ptr<Item>>(creature_bite)->to_json(profile);
        if (std::holds_alternative<std::unique_ptr<Item>>(creature_skin))
            j["creature_skin"] = std::get<std::unique_ptr<Item>>(creature_skin)->to_json(profile);
    }
    return j;
}

} // namespace nw
