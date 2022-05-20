#include "Placeable.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool PlaceableScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("OnClick", on_click);
    archive.get_to("OnClosed", on_closed);
    archive.get_to("OnDamaged", on_damaged);
    archive.get_to("OnDeath", on_death);
    archive.get_to("OnDisarm", on_disarm);
    archive.get_to("OnHeartbeat", on_heartbeat);
    archive.get_to("OnInvDisturbed", on_inventory_disturbed);
    archive.get_to("OnLock", on_lock);
    archive.get_to("OnMeleeAttacked", on_melee_attacked);
    archive.get_to("OnOpen", on_open);
    archive.get_to("OnSpellCastAt", on_spell_cast_at);
    archive.get_to("OnTrapTriggered", on_trap_triggered);
    archive.get_to("OnUnlock", on_unlock);
    archive.get_to("OnUsed", on_used);
    archive.get_to("OnUserDefined", on_user_defined);

    return true;
}

bool PlaceableScripts::from_json(const nlohmann::json& archive)
{
    archive.at("on_click").get_to(on_click);
    archive.at("on_closed").get_to(on_closed);
    archive.at("on_damaged").get_to(on_damaged);
    archive.at("on_death").get_to(on_death);
    archive.at("on_disarm").get_to(on_disarm);
    archive.at("on_heartbeat").get_to(on_heartbeat);
    archive.at("on_inventory_disturbed").get_to(on_inventory_disturbed);
    archive.at("on_lock").get_to(on_lock);
    archive.at("on_melee_attacked").get_to(on_melee_attacked);
    archive.at("on_open").get_to(on_open);
    archive.at("on_spell_cast_at").get_to(on_spell_cast_at);
    archive.at("on_trap_triggered").get_to(on_trap_triggered);
    archive.at("on_unlock").get_to(on_unlock);
    archive.at("on_used").get_to(on_used);
    archive.at("on_user_defined").get_to(on_user_defined);

    return true;
}

bool PlaceableScripts::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_field("OnClick", on_click)
        .add_field("OnClosed", on_closed)
        .add_field("OnDamaged", on_damaged)
        .add_field("OnDeath", on_death)
        .add_field("OnDisarm", on_disarm)
        .add_field("OnHeartbeat", on_heartbeat)
        .add_field("OnInvDisturbed", on_inventory_disturbed)
        .add_field("OnLock", on_lock)
        .add_field("OnMeleeAttacked", on_melee_attacked)
        .add_field("OnOpen", on_open)
        .add_field("OnSpellCastAt", on_spell_cast_at)
        .add_field("OnTrapTriggered", on_trap_triggered)
        .add_field("OnUnlock", on_unlock)
        .add_field("OnUsed", on_used)
        .add_field("OnUserDefined", on_user_defined);

    return true;
}

nlohmann::json PlaceableScripts::to_json() const
{
    nlohmann::json j;

    j["on_click"] = on_click;
    j["on_closed"] = on_closed;
    j["on_damaged"] = on_damaged;
    j["on_death"] = on_death;
    j["on_disarm"] = on_disarm;
    j["on_heartbeat"] = on_heartbeat;
    j["on_inventory_disturbed"] = on_inventory_disturbed;
    j["on_lock"] = on_lock;
    j["on_melee_attacked"] = on_melee_attacked;
    j["on_open"] = on_open;
    j["on_spell_cast_at"] = on_spell_cast_at;
    j["on_trap_triggered"] = on_trap_triggered;
    j["on_unlock"] = on_unlock;
    j["on_used"] = on_used;
    j["on_user_defined"] = on_user_defined;

    return j;
}

bool Placeable::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto placeable = ent.get_mut<Placeable>();
    auto scripts = ent.get_mut<PlaceableScripts>();
    auto common = ent.get_mut<Common>();
    auto inventory = ent.get_mut<Inventory>();
    auto lock = ent.get_mut<Lock>();
    auto trap = ent.get_mut<Trap>();

    common->from_gff(archive, profile);

    archive.get_to("HasInventory", placeable->has_inventory);
    if (placeable->has_inventory) {
        inventory->from_gff(archive, profile);
    }

    lock->from_gff(archive);
    scripts->from_gff(archive);
    trap->from_gff(archive);

    archive.get_to("Conversation", placeable->conversation);
    archive.get_to("Description", placeable->description);
    uint8_t save;
    archive.get_to("Fort", save);
    placeable->saves.fort = save;
    archive.get_to("Ref", save);
    placeable->saves.reflex = save;
    archive.get_to("Will", save);
    placeable->saves.will = save;

    archive.get_to("Appearance", placeable->appearance);
    archive.get_to("Faction", placeable->faction);

    archive.get_to("HP", placeable->hp);
    archive.get_to("CurrentHP", placeable->hp_current);
    archive.get_to("PortraitId", placeable->portrait_id);

    archive.get_to("AnimationState", placeable->animation_state);
    archive.get_to("BodyBag", placeable->bodybag);
    archive.get_to("Hardness", placeable->hardness);
    archive.get_to("Interruptable", placeable->interruptable);
    archive.get_to("Plot", placeable->plot);
    archive.get_to("Static", placeable->static_);
    archive.get_to("Useable", placeable->useable);

    return true;
}

bool Placeable::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto placeable = ent.get_mut<Placeable>();
    auto scripts = ent.get_mut<PlaceableScripts>();
    auto common = ent.get_mut<Common>();
    auto inventory = ent.get_mut<Inventory>();
    auto lock = ent.get_mut<Lock>();
    auto trap = ent.get_mut<Trap>();

    try {
        common->from_json(archive.at("common"), profile);
        inventory->from_json(archive.at("inventory"), profile);
        lock->from_json(archive.at("lock"));
        scripts->from_json(archive.at("scripts"));
        trap->from_json(archive.at("trap"));

        archive.at("conversation").get_to(placeable->conversation);
        archive.at("description").get_to(placeable->description);
        archive.at("saves").get_to(placeable->saves);

        archive.at("appearance").get_to(placeable->appearance);
        archive.at("faction").get_to(placeable->faction);

        archive.at("hp").get_to(placeable->hp);
        archive.at("hp_current").get_to(placeable->hp_current);
        archive.at("portrait_id").get_to(placeable->portrait_id);

        archive.at("animation_state").get_to(placeable->animation_state);
        archive.at("bodybag").get_to(placeable->bodybag);
        archive.at("has_inventory").get_to(placeable->has_inventory);
        archive.at("hardness").get_to(placeable->hardness);
        archive.at("interruptable").get_to(placeable->interruptable);
        archive.at("plot").get_to(placeable->plot);
        archive.at("static").get_to(placeable->static_);
        archive.at("useable").get_to(placeable->useable);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Placeable::from_json exception {}", e.what());
        return false;
    }
    return true;
}

bool Placeable::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto placeable = ent.get<Placeable>();
    auto scripts = ent.get<PlaceableScripts>();
    auto common = ent.get<Common>();
    auto inventory = ent.get<Inventory>();
    auto lock = ent.get<Lock>();
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

    inventory->to_gff(archive, profile);
    lock->to_gff(archive);
    scripts->to_gff(archive);
    trap->to_gff(archive);

    archive.add_field("Conversation", placeable->conversation)
        .add_field("Description", placeable->description);

    archive.add_field("Fort", static_cast<uint8_t>(placeable->saves.fort))
        .add_field("Ref", static_cast<uint8_t>(placeable->saves.reflex))
        .add_field("Will", static_cast<uint8_t>(placeable->saves.will));

    archive.add_field("Appearance", placeable->appearance)
        .add_field("Faction", placeable->faction);

    archive.add_field("HP", placeable->hp)
        .add_field("CurrentHP", placeable->hp_current)
        .add_field("PortraitId", placeable->portrait_id);

    uint8_t type = 0;
    archive.add_field("Type", type) // Obsolete, unused
        .add_field("AnimationState", placeable->animation_state)
        .add_field("BodyBag", placeable->bodybag)
        .add_field("HasInventory", placeable->has_inventory)
        .add_field("Hardness", placeable->hardness)
        .add_field("Interruptable", placeable->interruptable)
        .add_field("Plot", placeable->plot)
        .add_field("Static", placeable->static_)
        .add_field("Useable", placeable->useable);

    return true;
}

GffOutputArchive Placeable::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTP"};
    Placeable::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Placeable::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto placeable = ent.get<Placeable>();
    auto scripts = ent.get<PlaceableScripts>();
    auto common = ent.get<Common>();
    auto inventory = ent.get<Inventory>();
    auto lock = ent.get<Lock>();
    auto trap = ent.get<Trap>();

    archive["$type"] = "UTP";
    archive["$version"] = json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["inventory"] = inventory->to_json(profile);
    archive["lock"] = lock->to_json();
    archive["scripts"] = scripts->to_json();
    archive["trap"] = trap->to_json();

    archive["description"] = placeable->description;
    archive["conversation"] = placeable->conversation;
    archive["saves"] = placeable->saves;

    archive["appearance"] = placeable->appearance;
    archive["faction"] = placeable->faction;

    archive["hp"] = placeable->hp;
    archive["hp_current"] = placeable->hp_current;
    archive["portrait_id"] = placeable->portrait_id;

    archive["animation_state"] = placeable->animation_state;
    archive["bodybag"] = placeable->bodybag;
    archive["has_inventory"] = placeable->has_inventory;
    archive["useable"] = placeable->useable;
    archive["static"] = placeable->static_;
    archive["hardness"] = placeable->hardness;
    archive["interruptable"] = placeable->interruptable;
    archive["plot"] = placeable->plot;

    return true;
}

} // namespace nw
