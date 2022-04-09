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

Placeable::Placeable()
    : common_{ObjectType::placeable}
    , inventory{this}
{
    valid_ = true;
}

Placeable::Placeable(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::placeable}
    , inventory{this}
{
    valid_ = this->from_gff(archive, profile);
}

Placeable::Placeable(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::placeable}
    , inventory{this}
{
    valid_ = this->from_json(archive, profile);
}

bool Placeable::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    common_.from_gff(archive, profile);
    archive.get_to("Conversation", conversation);
    archive.get_to("Description", description);

    archive.get_to("HasInventory", has_inventory);
    if (has_inventory) {
        inventory.from_gff(archive, profile);
    }

    lock.from_gff(archive);

    uint8_t save;
    archive.get_to("Fort", save);
    saves.fort = save;
    archive.get_to("Ref", save);
    saves.reflex = save;
    archive.get_to("Will", save);
    saves.will = save;

    scripts.from_gff(archive);
    trap.from_gff(archive);

    archive.get_to("Appearance", appearance);
    archive.get_to("Faction", faction);

    archive.get_to("HP", hp);
    archive.get_to("CurrentHP", hp_current);
    archive.get_to("PortraitId", portrait_id);

    archive.get_to("AnimationState", animation_state);
    archive.get_to("BodyBag", bodybag);
    archive.get_to("Hardness", hardness);
    archive.get_to("Interruptable", interruptable);
    archive.get_to("Plot", plot);
    archive.get_to("Static", static_);
    archive.get_to("Useable", useable);

    return true;
}

bool Placeable::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        common_.from_json(archive.at("common"), profile);
        archive.at("conversation").get_to(conversation);
        archive.at("description").get_to(description);
        inventory.from_json(archive.at("inventory"), profile);
        lock.from_json(archive.at("lock"));
        archive.at("saves").get_to(saves);
        scripts.from_json(archive.at("scripts"));
        trap.from_json(archive.at("trap"));

        archive.at("appearance").get_to(appearance);
        archive.at("faction").get_to(faction);

        archive.at("hp").get_to(hp);
        archive.at("hp_current").get_to(hp_current);
        archive.at("portrait_id").get_to(portrait_id);

        archive.at("animation_state").get_to(animation_state);
        archive.at("bodybag").get_to(bodybag);
        archive.at("has_inventory").get_to(has_inventory);
        archive.at("hardness").get_to(hardness);
        archive.at("interruptable").get_to(interruptable);
        archive.at("plot").get_to(plot);
        archive.at("static").get_to(static_);
        archive.at("useable").get_to(useable);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Placeable::from_json exception {}", e.what());
        return false;
    }
    return true;
}

bool Placeable::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{

    archive.add_field("TemplateResRef", common_.resref)
        .add_field("LocName", common_.name)
        .add_field("Tag", common_.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("PaletteID", common_.palette_id);
    } else {
        archive.add_field("PositionX", common_.location.position.x)
            .add_field("PositionY", common_.location.position.y)
            .add_field("PositionZ", common_.location.position.z)
            .add_field("OrientationX", common_.location.orientation.x)
            .add_field("OrientationY", common_.location.orientation.y);
    }

    if (common_.locals.size()) {
        common_.locals.to_gff(archive);
    }
    archive.add_field("Conversation", conversation)
        .add_field("Description", description);

    inventory.to_gff(archive, profile);
    lock.to_gff(archive);
    archive.add_field("Fort", static_cast<uint8_t>(saves.fort))
        .add_field("Ref", static_cast<uint8_t>(saves.reflex))
        .add_field("Will", static_cast<uint8_t>(saves.will));
    scripts.to_gff(archive);
    trap.to_gff(archive);

    archive.add_field("Appearance", appearance)
        .add_field("Faction", faction);

    archive.add_field("HP", hp)
        .add_field("CurrentHP", hp_current)
        .add_field("PortraitId", portrait_id);

    uint8_t type = 0;
    archive.add_field("Type", type) // Obsolete, unused
        .add_field("AnimationState", animation_state)
        .add_field("BodyBag", bodybag)
        .add_field("HasInventory", has_inventory)
        .add_field("Hardness", hardness)
        .add_field("Interruptable", interruptable)
        .add_field("Plot", plot)
        .add_field("Static", static_)
        .add_field("Useable", useable);

    return true;
}

GffOutputArchive Placeable::to_gff(SerializationProfile profile) const
{
    GffOutputArchive out{"UTP"};
    this->to_gff(out.top, profile);
    out.build();
    return out;
}

nlohmann::json Placeable::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTP";
    j["$version"] = json_archive_version;

    j["common"] = common_.to_json(profile);
    j["description"] = description;
    j["conversation"] = conversation;
    j["inventory"] = inventory.to_json(profile);
    j["lock"] = lock.to_json();
    j["saves"] = saves;
    j["scripts"] = scripts.to_json();
    j["trap"] = trap.to_json();

    j["appearance"] = appearance;
    j["faction"] = faction;

    j["hp"] = hp;
    j["hp_current"] = hp_current;
    j["portrait_id"] = portrait_id;

    j["animation_state"] = animation_state;
    j["bodybag"] = bodybag;
    j["has_inventory"] = has_inventory;
    j["useable"] = useable;
    j["static"] = static_;
    j["hardness"] = hardness;
    j["interruptable"] = interruptable;
    j["plot"] = plot;

    return j;
}

} // namespace nw
