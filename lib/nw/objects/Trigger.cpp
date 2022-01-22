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

    if (profile != SerializationProfile::blueprint) {
        size_t sz = archive["Geometry"].size();
        geometery.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("PointX", v[0]);
            archive["Geometry"][i].get_to("PointY", v[1]);
            archive["Geometry"][i].get_to("PointZ", v[2]);
            geometery.push_back(v);
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

        if (profile != SerializationProfile::blueprint) {
            auto& ref = archive.at("geometery");
            for (size_t i = 0; i < ref.size(); ++i) {
                geometery.emplace_back(ref[i][0].get<float>(), ref[i][1].get<float>(), ref[i][2].get<float>());
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Trigger::from_json exception: {}", e.what());
        return valid_ = false;
    }
    return valid_ = true;
}

nlohmann::json Trigger::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    j["$type"] = "UTT";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

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

    if (profile != SerializationProfile::blueprint) {
        auto& ref = j["geometery"] = nlohmann::json::array();
        for (const auto& g : geometery) {
            ref.push_back({g.x, g.y, g.z});
        }
    }

    return j;
}

} // namespace nw
