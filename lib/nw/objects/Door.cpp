#include "Door.hpp"

#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool DoorScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("OnClick", on_click);
    archive.get_to("OnClosed", on_closed);
    archive.get_to("OnDamaged", on_damaged);
    archive.get_to("OnDeath", on_death);
    archive.get_to("OnDisarm", on_disarm);
    archive.get_to("OnHeartbeat", on_heartbeat);
    archive.get_to("OnLock", on_lock);
    archive.get_to("OnMeleeAttacked", on_melee_attacked);
    archive.get_to("OnOpen", on_open);
    archive.get_to("OnFailToOpen", on_open_failure);
    archive.get_to("OnSpellCastAt", on_spell_cast_at);
    archive.get_to("OnTrapTriggered", on_trap_triggered);
    archive.get_to("OnUnlock", on_unlock);
    archive.get_to("OnUserDefined", on_user_defined);

    return true;
}

bool DoorScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_click").get_to(on_click);
        archive.at("on_closed").get_to(on_closed);
        archive.at("on_damaged").get_to(on_damaged);
        archive.at("on_death").get_to(on_death);
        archive.at("on_disarm").get_to(on_disarm);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_lock").get_to(on_lock);
        archive.at("on_melee_attacked").get_to(on_melee_attacked);
        archive.at("on_open").get_to(on_open);
        archive.at("on_open_failure").get_to(on_open_failure);
        archive.at("on_spell_cast_at").get_to(on_spell_cast_at);
        archive.at("on_trap_triggered").get_to(on_trap_triggered);
        archive.at("on_unlock").get_to(on_unlock);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "DoorScripts::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool DoorScripts::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_field("OnClick", on_click)
        .add_field("OnClosed", on_closed)
        .add_field("OnDamaged", on_damaged)
        .add_field("OnDeath", on_death)
        .add_field("OnDisarm", on_disarm)
        .add_field("OnHeartbeat", on_heartbeat)
        .add_field("OnLock", on_lock)
        .add_field("OnMeleeAttacked", on_melee_attacked)
        .add_field("OnOpen", on_open)
        .add_field("OnFailToOpen", on_open_failure)
        .add_field("OnSpellCastAt", on_spell_cast_at)
        .add_field("OnTrapTriggered", on_trap_triggered)
        .add_field("OnUnlock", on_unlock)
        .add_field("OnUserDefined", on_user_defined);

    return true;
}

nlohmann::json DoorScripts::to_json() const
{
    nlohmann::json j;

    j["on_click"] = on_click;
    j["on_closed"] = on_closed;
    j["on_damaged"] = on_damaged;
    j["on_death"] = on_death;
    j["on_disarm"] = on_disarm;
    j["on_heartbeat"] = on_heartbeat;
    j["on_lock"] = on_lock;
    j["on_melee_attacked"] = on_melee_attacked;
    j["on_open"] = on_open;
    j["on_open_failure"] = on_open_failure;
    j["on_spell_cast_at"] = on_spell_cast_at;
    j["on_trap_triggered"] = on_trap_triggered;
    j["on_unlock"] = on_unlock;
    j["on_user_defined"] = on_user_defined;

    return j;
}

bool Door::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto door = ent.get_mut<Door>();
    auto common = ent.get_mut<Common>();
    auto lock = ent.get_mut<Lock>();
    auto scripts = ent.get_mut<DoorScripts>();
    auto trap = ent.get_mut<Trap>();

    common->from_gff(archive, profile);
    lock->from_gff(archive);
    scripts->from_gff(archive);
    trap->from_gff(archive);

    bool result = true;
    uint8_t save;

    archive.get_to("Conversation", door->conversation);
    archive.get_to("Description", door->description);
    archive.get_to("LinkedTo", door->linked_to);

    archive.get_to("Fort", save);
    door->saves.fort = save;
    archive.get_to("Ref", save);
    door->saves.reflex = save;
    archive.get_to("Will", save);
    door->saves.will = save;

    archive.get_to("Appearance", door->appearance);
    archive.get_to("Faction", door->faction);
    if (!archive.get_to("GenericType_New", door->generic_type, false)) {
        uint8_t temp = 0;
        archive.get_to("GenericType", temp);
        door->generic_type = temp;
    }

    archive.get_to("HP", door->hp);
    archive.get_to("CurrentHP", door->hp_current);
    archive.get_to("LoadScreenID", door->loadscreen);
    archive.get_to("PortraitId", door->portrait_id);

    archive.get_to("AnimationState", door->animation_state);
    archive.get_to("Hardness", door->hardness);
    archive.get_to("Interruptable", door->interruptable);
    archive.get_to("LinkedToFlags", door->linked_to_flags);
    archive.get_to("Plot", door->plot);

    return result;
}

bool Door::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto door = ent.get_mut<Door>();
    auto common = ent.get_mut<Common>();
    auto lock = ent.get_mut<Lock>();
    auto scripts = ent.get_mut<DoorScripts>();
    auto trap = ent.get_mut<Trap>();

    try {
        common->from_json(archive.at("common"), profile);
        lock->from_json(archive.at("lock"));
        scripts->from_json(archive.at("scripts"));
        trap->from_json(archive.at("trap"));

        archive.at("conversation").get_to(door->conversation);
        archive.at("description").get_to(door->description);
        archive.at("saves").get_to(door->saves);

        archive.at("appearance").get_to(door->appearance);
        archive.at("faction").get_to(door->faction);
        archive.at("generic_type").get_to(door->generic_type);

        archive.at("hp").get_to(door->hp);
        archive.at("hp_current").get_to(door->hp_current);
        archive.at("loadscreen").get_to(door->loadscreen);
        archive.at("portrait_id").get_to(door->portrait_id);

        archive.at("animation_state").get_to(door->animation_state);
        archive.at("hardness").get_to(door->hardness);
        archive.at("interruptable").get_to(door->interruptable);
        archive.at("linked_to_flags").get_to(door->linked_to_flags);
        archive.at("linked_to").get_to(door->linked_to);
        archive.at("plot").get_to(door->plot);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Door::to_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Door::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto door = ent.get<Door>();
    auto common = ent.get<Common>();
    auto lock = ent.get<Lock>();
    auto scripts = ent.get<DoorScripts>();
    auto trap = ent.get<Trap>();

    archive.add_field("TemplateResRef", common->resref)
        .add_field("LocName", common->name)
        .add_field("Tag", common->tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common->comment);
        archive.add_field("PaletteID", common->palette_id);
    } else {
        archive.add_field("PositionX", common->location.position.x)
            .add_field("PositionY", common->location.position.y)
            .add_field("PositionZ", common->location.position.z)
            .add_field("OrientationX", common->location.orientation.x)
            .add_field("OrientationY", common->location.orientation.y);
    }

    if (common->locals.size()) {
        common->locals.to_gff(archive);
    }

    lock->to_gff(archive);
    scripts->to_gff(archive);
    trap->to_gff(archive);

    archive.add_field("Conversation", door->conversation)
        .add_field("Description", door->description)
        .add_field("LinkedTo", door->linked_to);

    archive.add_field("Fort", static_cast<uint8_t>(door->saves.fort))
        .add_field("Ref", static_cast<uint8_t>(door->saves.reflex))
        .add_field("Will", static_cast<uint8_t>(door->saves.will));

    archive.add_field("Appearance", door->appearance)
        .add_field("Faction", door->faction)
        .add_field("GenericType_New", door->generic_type);

    archive.add_field("HP", door->hp)
        .add_field("CurrentHP", door->hp_current)
        .add_field("LoadScreenID", door->loadscreen)
        .add_field("PortraitId", door->portrait_id);

    archive.add_field("AnimationState", door->animation_state)
        .add_field("Hardness", door->hardness)
        .add_field("Interruptable", door->interruptable)
        .add_field("LinkedToFlags", door->linked_to_flags)
        .add_field("Plot", door->plot);

    return true;
}

GffOutputArchive Door::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTD"};
    Door::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Door::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto door = ent.get<Door>();
    auto common = ent.get<Common>();
    auto lock = ent.get<Lock>();
    auto scripts = ent.get<DoorScripts>();
    auto trap = ent.get<Trap>();

    archive["$type"] = "UTD";
    archive["$version"] = Door::json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["lock"] = lock->to_json();
    archive["scripts"] = scripts->to_json();
    archive["trap"] = trap->to_json();

    archive["conversation"] = door->conversation;
    archive["description"] = door->description;
    archive["linked_to"] = door->linked_to;
    archive["saves"] = door->saves;

    archive["appearance"] = door->appearance;
    archive["faction"] = door->faction;
    archive["generic_type"] = door->generic_type;

    archive["hp"] = door->hp;
    archive["hp_current"] = door->hp_current;
    archive["loadscreen"] = door->loadscreen;
    archive["portrait_id"] = door->portrait_id;

    archive["animation_state"] = door->animation_state;
    archive["hardness"] = door->hardness;
    archive["interruptable"] = door->interruptable;
    archive["linked_to_flags"] = door->linked_to_flags;
    archive["plot"] = door->plot;

    return true;
}

} // namespace nw
