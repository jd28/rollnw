#include "Trigger.hpp"

#include <nlohmann/json.hpp>

namespace nw {

nlohmann::json TriggerScripts::to_json() const
{
    nlohmann::json j;
    j["on_click"] = on_click;
    j["on_disarm"] = on_disarm;
    j["on_enter"] = on_enter;
    j["on_exit"] = on_exit;
    j["on_heartbeat"] = on_heartbeat;
    j["on_trap_triggered"] = on_trap_triggered;
    j["on_user_defined"] = on_user_defined;
    return j;
}

bool TriggerScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_click").get_to(on_click);
        archive.at("on_disarm").get_to(on_disarm);
        archive.at("on_enter").get_to(on_enter);
        archive.at("on_exit").get_to(on_exit);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_trap_triggered").get_to(on_trap_triggered);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "trigger/scripts: json parsing error: {}", e.what());
        return false;
    }

    return true;
}

Trigger::Trigger()
    : common_{ObjectType::trigger}
{
}

Trigger::Trigger(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::trigger}
{
    valid_ = this->from_gff(archive, profile);
}

Trigger::Trigger(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::trigger}
{
    valid_ = this->from_json(archive, profile);
}

bool Trigger::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    common_.from_gff(archive, profile);
    trap.from_gff(archive);

    archive.get_to("Faction", faction);
    archive.get_to("Cursor", cursor);
    archive.get_to("HighlightHeight", highlight_height);
    archive.get_to("LinkedTo", linked_to);
    archive.get_to("LinkedToFlags", linked_to_flags);
    archive.get_to("LoadScreenID", loadscreen);
    archive.get_to("OnClick", scripts.on_click);
    archive.get_to("OnDisarm", scripts.on_disarm);
    archive.get_to("OnTrapTriggered", scripts.on_trap_triggered);
    archive.get_to("PortraitId", portrait);
    archive.get_to("ScriptHeartbeat", scripts.on_heartbeat);
    archive.get_to("ScriptOnEnter", scripts.on_enter);
    archive.get_to("ScriptOnExit", scripts.on_exit);
    archive.get_to("ScriptUserDefine", scripts.on_user_defined);
    archive.get_to("Type", type);

    if (profile != SerializationProfile::blueprint) {
        size_t sz = archive["Geometry"].size();
        geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("PointX", v[0]);
            archive["Geometry"][i].get_to("PointY", v[1]);
            archive["Geometry"][i].get_to("PointZ", v[2]);
            geometry.push_back(v);
        }
    }
    return true;
}

bool Trigger::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        if (!common_.from_json(archive.at("common"), profile)
            || !trap.from_json(archive.at("trap"))
            || !scripts.from_json(archive.at("scripts"))) {
            return valid_ = false;
        }

        archive.at("faction").get_to(faction);
        archive.at("cursor").get_to(cursor);
        archive.at("highlight_height").get_to(highlight_height);
        archive.at("linked_to_flags").get_to(linked_to_flags);
        archive.at("linked_to").get_to(linked_to);
        archive.at("loadscreen").get_to(loadscreen);
        archive.at("portrait").get_to(portrait);
        archive.at("type").get_to(type);

        if (profile != SerializationProfile::blueprint) {
            auto& ref = archive.at("geometry");
            for (size_t i = 0; i < ref.size(); ++i) {
                geometry.emplace_back(ref[i][0].get<float>(), ref[i][1].get<float>(), ref[i][2].get<float>());
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Trigger::from_json exception: {}", e.what());
        return valid_ = false;
    }
    return valid_ = true;
}

bool Trigger::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    archive.add_field("TemplateResRef", common_.resref); // Store does it's own thing, not typo.
    archive.add_field("LocalizedName", common_.name);
    archive.add_field("Tag", common_.tag);

    trap.to_gff(archive);

    uint8_t zero = 0;
    std::string empty;

    archive.add_field("Faction", faction)
        .add_field("Cursor", cursor)
        .add_field("HighlightHeight", highlight_height)
        .add_field("AutoRemoveKey", zero) // obsolete
        .add_field("KeyName", empty)      // obsolete
        .add_field("LinkedTo", linked_to)
        .add_field("LinkedToFlags", linked_to_flags)
        .add_field("LoadScreenID", loadscreen)
        .add_field("OnClick", scripts.on_click)
        .add_field("OnDisarm", scripts.on_disarm)
        .add_field("OnTrapTriggered", scripts.on_trap_triggered)
        .add_field("PortraitId", portrait)
        .add_field("ScriptHeartbeat", scripts.on_heartbeat)
        .add_field("ScriptOnEnter", scripts.on_enter)
        .add_field("ScriptOnExit", scripts.on_exit)
        .add_field("ScriptUserDefine", scripts.on_user_defined)
        .add_field("Type", type);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("PaletteID", common_.palette_id);
    } else {
        archive.add_field("PositionX", common_.location.position.x)
            .add_field("PositionY", common_.location.position.y)
            .add_field("PositionZ", common_.location.position.z)
            .add_field("OrientationX", common_.location.orientation.x)
            .add_field("OrientationY", common_.location.orientation.y);

        auto& list = archive.add_list("Geometry");
        for (const auto& point : geometry) {
            list.push_back(3)
                .add_field("PointX", point[0])
                .add_field("PointY", point[1])
                .add_field("PointZ", point[2]);
        }
    }
    return true;
}

GffOutputArchive Trigger::to_gff(SerializationProfile profile) const
{
    GffOutputArchive out{"UTT"};
    this->to_gff(out.top, profile);
    out.build();
    return out;
}

nlohmann::json Trigger::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    j["$type"] = "UTT";
    j["$version"] = json_archive_version;

    j["common"] = common_.to_json(profile);
    j["scripts"] = scripts.to_json();
    j["trap"] = trap.to_json();

    j["faction"] = faction;
    j["cursor"] = cursor;
    j["highlight_height"] = highlight_height;
    j["linked_to_flags"] = linked_to_flags;
    j["linked_to"] = linked_to;
    j["loadscreen"] = loadscreen;
    j["portrait"] = portrait;
    j["type"] = type;

    if (profile != SerializationProfile::blueprint) {
        auto& ref = j["geometry"] = nlohmann::json::array();
        for (const auto& g : geometry) {
            ref.push_back({g.x, g.y, g.z});
        }
    }

    return j;
}

} // namespace nw
