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

bool Trigger::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto trigger = ent.get_mut<Trigger>();
    auto scripts = ent.get_mut<TriggerScripts>();
    auto common = ent.get_mut<Common>();
    auto trap = ent.get_mut<Trap>();

    common->from_gff(archive, profile);

    archive.get_to("OnClick", scripts->on_click);
    archive.get_to("OnDisarm", scripts->on_disarm);
    archive.get_to("ScriptOnEnter", scripts->on_enter);
    archive.get_to("ScriptOnExit", scripts->on_exit);
    archive.get_to("ScriptHeartbeat", scripts->on_heartbeat);
    archive.get_to("OnTrapTriggered", scripts->on_trap_triggered);
    archive.get_to("ScriptUserDefine", scripts->on_user_defined);

    trap->from_gff(archive);

    if (profile != SerializationProfile::blueprint) {
        size_t sz = archive["Geometry"].size();
        trigger->geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("PointX", v[0]);
            archive["Geometry"][i].get_to("PointY", v[1]);
            archive["Geometry"][i].get_to("PointZ", v[2]);
            trigger->geometry.push_back(v);
        }
    }

    archive.get_to("LinkedTo", trigger->linked_to);

    archive.get_to("Faction", trigger->faction);
    archive.get_to("HighlightHeight", trigger->highlight_height);
    archive.get_to("Type", trigger->type);

    archive.get_to("LoadScreenID", trigger->loadscreen);
    archive.get_to("PortraitId", trigger->portrait);

    archive.get_to("Cursor", trigger->cursor);
    archive.get_to("LinkedToFlags", trigger->linked_to_flags);

    return true;
}

bool Trigger::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto trigger = ent.get_mut<Trigger>();
    auto scripts = ent.get_mut<TriggerScripts>();
    auto common = ent.get_mut<Common>();
    auto trap = ent.get_mut<Trap>();

    try {
        if (!common->from_json(archive.at("common"), profile)
            || !scripts->from_json(archive.at("scripts"))
            || !trap->from_json(archive.at("trap"))) {
            return false;
        }

        if (profile != SerializationProfile::blueprint) {
            auto& ref = archive.at("geometry");
            for (size_t i = 0; i < ref.size(); ++i) {
                trigger->geometry.emplace_back(ref[i][0].get<float>(), ref[i][1].get<float>(), ref[i][2].get<float>());
            }
        }

        archive.at("linked_to").get_to(trigger->linked_to);

        archive.at("faction").get_to(trigger->faction);
        archive.at("highlight_height").get_to(trigger->highlight_height);
        archive.at("type").get_to(trigger->type);

        archive.at("loadscreen").get_to(trigger->loadscreen);
        archive.at("portrait").get_to(trigger->portrait);

        archive.at("cursor").get_to(trigger->cursor);
        archive.at("linked_to_flags").get_to(trigger->linked_to_flags);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Trigger::from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool Trigger::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto trigger = ent.get<Trigger>();
    auto scripts = ent.get<TriggerScripts>();
    auto common = ent.get<Common>();
    auto trap = ent.get<Trap>();

    archive.add_field("TemplateResRef", common->resref); // Store does it's own thing, not typo.
    archive.add_field("LocalizedName", common->name);
    archive.add_field("Tag", common->tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common->comment);
        archive.add_field("PaletteID", common->palette_id);
    } else {
        archive.add_field("PositionX", common->location.position.x)
            .add_field("PositionY", common->location.position.y)
            .add_field("PositionZ", common->location.position.z)
            .add_field("OrientationX", common->location.orientation.x)
            .add_field("OrientationY", common->location.orientation.y);

        auto& list = archive.add_list("Geometry");
        for (const auto& point : trigger->geometry) {
            list.push_back(3)
                .add_field("PointX", point[0])
                .add_field("PointY", point[1])
                .add_field("PointZ", point[2]);
        }
    }

    archive.add_field("LinkedTo", trigger->linked_to)
        .add_field("OnClick", scripts->on_click)
        .add_field("OnDisarm", scripts->on_disarm)
        .add_field("ScriptOnEnter", scripts->on_enter)
        .add_field("ScriptOnExit", scripts->on_exit)
        .add_field("ScriptHeartbeat", scripts->on_heartbeat)
        .add_field("OnTrapTriggered", scripts->on_trap_triggered)
        .add_field("ScriptUserDefine", scripts->on_user_defined);

    trap->to_gff(archive);

    uint8_t zero = 0;
    std::string empty;

    archive.add_field("Faction", trigger->faction)
        .add_field("HighlightHeight", trigger->highlight_height)
        .add_field("Type", trigger->type);

    archive.add_field("LoadScreenID", trigger->loadscreen)
        .add_field("PortraitId", trigger->portrait);

    archive.add_field("Cursor", trigger->cursor)
        .add_field("LinkedToFlags", trigger->linked_to_flags)
        .add_field("AutoRemoveKey", zero) // obsolete
        .add_field("KeyName", empty);     // obsolete

    return true;
}

GffOutputArchive Trigger::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTT"};
    Trigger::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Trigger::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto trigger = ent.get<Trigger>();
    auto scripts = ent.get<TriggerScripts>();
    auto common = ent.get<Common>();
    auto trap = ent.get<Trap>();

    archive["$type"] = "UTT";
    archive["$version"] = json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["scripts"] = scripts->to_json();
    archive["trap"] = trap->to_json();

    if (profile != SerializationProfile::blueprint) {
        auto& ref = archive["geometry"] = nlohmann::json::array();
        for (const auto& g : trigger->geometry) {
            ref.push_back({g.x, g.y, g.z});
        }
    }

    archive["linked_to"] = trigger->linked_to;

    archive["faction"] = trigger->faction;
    archive["highlight_height"] = trigger->highlight_height;
    archive["type"] = trigger->type;

    archive["loadscreen"] = trigger->loadscreen;
    archive["portrait"] = trigger->portrait;

    archive["cursor"] = trigger->cursor;
    archive["linked_to_flags"] = trigger->linked_to_flags;

    return true;
}

} // namespace nw
