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
{
    set_handle({object_invalid, ObjectType::trigger, 0});
}

Versus Trigger::versus_me() const
{
    Versus result;
    result.trap = trap.is_trapped;
    return result;
}

bool Trigger::deserialize(Trigger* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) return false;

    try {
        if (!obj->common.from_json(archive.at("common"), profile, ObjectType::trigger)
            || !obj->scripts.from_json(archive.at("scripts"))
            || !obj->trap.from_json(archive.at("trap"))) {
            return false;
        }

        if (profile != SerializationProfile::blueprint) {
            auto& ref = archive.at("geometry");
            for (size_t i = 0; i < ref.size(); ++i) {
                obj->geometry.emplace_back(ref[i][0].get<float>(), ref[i][1].get<float>(), ref[i][2].get<float>());
            }
        }

        archive.at("linked_to").get_to(obj->linked_to);

        archive.at("faction").get_to(obj->faction);
        archive.at("highlight_height").get_to(obj->highlight_height);
        archive.at("type").get_to(obj->type);

        archive.at("loadscreen").get_to(obj->loadscreen);
        archive.at("portrait").get_to(obj->portrait);

        archive.at("cursor").get_to(obj->cursor);
        archive.at("linked_to_flags").get_to(obj->linked_to_flags);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Trigger::from_json exception: {}", e.what());
        return false;
    }

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool Trigger::serialize(const Trigger* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTT";
    archive["$version"] = json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::trigger);
    archive["scripts"] = obj->scripts.to_json();
    archive["trap"] = obj->trap.to_json();

    if (profile != SerializationProfile::blueprint) {
        auto& ref = archive["geometry"] = nlohmann::json::array();
        for (const auto& g : obj->geometry) {
            ref.push_back({g.x, g.y, g.z});
        }
    }

    archive["linked_to"] = obj->linked_to;

    archive["faction"] = obj->faction;
    archive["highlight_height"] = obj->highlight_height;
    archive["type"] = obj->type;

    archive["loadscreen"] = obj->loadscreen;
    archive["portrait"] = obj->portrait;

    archive["cursor"] = obj->cursor;
    archive["linked_to_flags"] = obj->linked_to_flags;

    return true;
}

// == Trigger - Serialization - Gff ===========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Trigger* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) return false;

    deserialize(obj->common, archive, profile, ObjectType::trigger);

    archive.get_to("OnClick", obj->scripts.on_click);
    archive.get_to("OnDisarm", obj->scripts.on_disarm);
    archive.get_to("ScriptOnEnter", obj->scripts.on_enter);
    archive.get_to("ScriptOnExit", obj->scripts.on_exit);
    archive.get_to("ScriptHeartbeat", obj->scripts.on_heartbeat);
    archive.get_to("OnTrapTriggered", obj->scripts.on_trap_triggered);
    archive.get_to("ScriptUserDefine", obj->scripts.on_user_defined);

    deserialize(obj->trap, archive);

    if (profile != SerializationProfile::blueprint) {
        size_t sz = archive["Geometry"].size();
        obj->geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("PointX", v[0]);
            archive["Geometry"][i].get_to("PointY", v[1]);
            archive["Geometry"][i].get_to("PointZ", v[2]);
            obj->geometry.push_back(v);
        }
    }

    archive.get_to("LinkedTo", obj->linked_to);

    archive.get_to("Faction", obj->faction);
    archive.get_to("HighlightHeight", obj->highlight_height);
    archive.get_to("Type", obj->type);

    archive.get_to("LoadScreenID", obj->loadscreen);
    archive.get_to("PortraitId", obj->portrait);

    archive.get_to("Cursor", obj->cursor);
    archive.get_to("LinkedToFlags", obj->linked_to_flags);

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool serialize(const Trigger* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) return false;

    archive.add_field("TemplateResRef", obj->common.resref); // Store does it's own thing, not typo.
    archive.add_field("LocalizedName", obj->common.name);
    archive.add_field("Tag", obj->common.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj->common.comment);
        archive.add_field("PaletteID", obj->common.palette_id);
    } else {
        archive.add_field("PositionX", obj->common.location.position.x)
            .add_field("PositionY", obj->common.location.position.y)
            .add_field("PositionZ", obj->common.location.position.z)
            .add_field("OrientationX", obj->common.location.orientation.x)
            .add_field("OrientationY", obj->common.location.orientation.y);

        auto& list = archive.add_list("Geometry");
        for (const auto& point : obj->geometry) {
            list.push_back(3)
                .add_field("PointX", point[0])
                .add_field("PointY", point[1])
                .add_field("PointZ", point[2]);
        }
    }

    archive.add_field("LinkedTo", obj->linked_to)
        .add_field("OnClick", obj->scripts.on_click)
        .add_field("OnDisarm", obj->scripts.on_disarm)
        .add_field("ScriptOnEnter", obj->scripts.on_enter)
        .add_field("ScriptOnExit", obj->scripts.on_exit)
        .add_field("ScriptHeartbeat", obj->scripts.on_heartbeat)
        .add_field("OnTrapTriggered", obj->scripts.on_trap_triggered)
        .add_field("ScriptUserDefine", obj->scripts.on_user_defined);

    serialize(obj->trap, archive);

    uint8_t zero = 0;
    std::string empty;

    archive.add_field("Faction", obj->faction)
        .add_field("HighlightHeight", obj->highlight_height)
        .add_field("Type", obj->type);

    archive.add_field("LoadScreenID", obj->loadscreen)
        .add_field("PortraitId", obj->portrait);

    archive.add_field("Cursor", obj->cursor)
        .add_field("LinkedToFlags", obj->linked_to_flags)
        .add_field("AutoRemoveKey", zero) // obsolete
        .add_field("KeyName", empty);     // obsolete

    return true;
}

GffBuilder serialize(const Trigger* obj, SerializationProfile profile)
{
    GffBuilder out{"UTT"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
