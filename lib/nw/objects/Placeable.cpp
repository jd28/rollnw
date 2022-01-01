#include "Placeable.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool PlaceableScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("OnClick", on_click);
    archive.get_to("OnInvDisturbed", on_inventory_disturbed);
    archive.get_to("OnUsed", on_used);
    archive.get_to("OnClosed", on_closed);
    archive.get_to("OnDamaged", on_damaged);
    archive.get_to("OnDeath", on_death);
    archive.get_to("OnDisarm", on_disarm);
    archive.get_to("OnHeartbeat", on_heartbeat);
    archive.get_to("OnLock", on_lock);
    archive.get_to("OnMeleeAttacked", on_melee_attacked);
    archive.get_to("OnOpen", on_open);
    archive.get_to("OnSpellCastAt", on_spell_cast_at);
    archive.get_to("OnTrapTriggered", on_trap_triggered);
    archive.get_to("OnUnlock", on_unlock);
    archive.get_to("OnUserDefined", on_user_defined);

    return true;
}

bool PlaceableScripts::from_json(const nlohmann::json& archive)
{
    archive.at("on_click").get_to(on_click);
    archive.at("on_inventory_disturbed").get_to(on_inventory_disturbed);
    archive.at("on_used").get_to(on_used);
    archive.at("on_closed").get_to(on_closed);
    archive.at("on_damaged").get_to(on_damaged);
    archive.at("on_death").get_to(on_death);
    archive.at("on_disarm").get_to(on_disarm);
    archive.at("on_heartbeat").get_to(on_heartbeat);
    archive.at("on_lock").get_to(on_lock);
    archive.at("on_melee_attacked").get_to(on_melee_attacked);
    archive.at("on_open").get_to(on_open);
    archive.at("on_spell_cast_at").get_to(on_spell_cast_at);
    archive.at("on_trap_triggered").get_to(on_trap_triggered);
    archive.at("on_unlock").get_to(on_unlock);
    archive.at("on_user_defined").get_to(on_user_defined);

    return true;
}

bool PlaceableScripts::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_fields({
        {"OnClick", on_click},
        {"OnInvDisturbed", on_inventory_disturbed},
        {"OnUsed", on_used},
        {"OnClosed", on_closed},
        {"OnDamaged", on_damaged},
        {"OnDeath", on_death},
        {"OnDisarm", on_disarm},
        {"OnHeartbeat", on_heartbeat},
        {"OnLock", on_lock},
        {"OnMeleeAttacked", on_melee_attacked},
        {"OnOpen", on_open},
        {"OnSpellCastAt", on_spell_cast_at},
        {"OnTrapTriggered", on_trap_triggered},
        {"OnUnlock", on_unlock},
        {"OnUserDefined", on_user_defined},
    });

    return true;
}

nlohmann::json PlaceableScripts::to_json() const
{
    nlohmann::json j;

    j["on_click"] = on_click;
    j["on_inventory_disturbed"] = on_inventory_disturbed;
    j["on_used"] = on_used;
    j["on_closed"] = on_closed;
    j["on_damaged"] = on_damaged;
    j["on_death"] = on_death;
    j["on_disarm"] = on_disarm;
    j["on_heartbeat"] = on_heartbeat;
    j["on_lock"] = on_lock;
    j["on_melee_attacked"] = on_melee_attacked;
    j["on_open"] = on_open;
    j["on_spell_cast_at"] = on_spell_cast_at;
    j["on_trap_triggered"] = on_trap_triggered;
    j["on_unlock"] = on_unlock;
    j["on_user_defined"] = on_user_defined;

    return j;
}

Placeable::Placeable()
    : common_{ObjectType::placeable}
{
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
    scripts.from_gff(archive);
    lock.from_gff(archive);
    trap.from_gff(archive);

    archive.get_to("AnimationState", animation_state);

    archive.get_to("BodyBag", bodybag);
    archive.get_to("HasInventory", has_inventory);
    archive.get_to("Static", static_);
    archive.get_to("Useable", useable);

    if (has_inventory) {
        inventory.from_gff(archive, profile);
    }

    uint8_t save;
    archive.get_to("Appearance", appearance);
    archive.get_to("Conversation", conversation);
    archive.get_to("Description", description);
    archive.get_to("Fort", save);
    saves.fort = save;
    archive.get_to("Hardness", hardness);
    archive.get_to("HP", hp);
    archive.get_to("CurrentHP", hp_current);
    archive.get_to("Interruptable", interruptable);
    archive.get_to("Plot", plot);
    archive.get_to("PortraitId", portrait_id);
    archive.get_to("Ref", save);
    saves.reflex = save;
    archive.get_to("Will", save);
    saves.will = save;

    return true;
}

bool Placeable::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        common_.from_json(archive.at("common"), profile);
        inventory.from_json(archive.at("inventory"), profile);
        lock.from_json(archive.at("lock"));
        scripts.from_json(archive.at("scripts"));
        trap.from_json(archive.at("trap"));

        archive.at("animation_state").get_to(animation_state);
        archive.at("bodybag").get_to(bodybag);
        archive.at("has_inventory").get_to(has_inventory);
        archive.at("useable").get_to(useable);
        archive.at("static").get_to(static_);
        archive.at("appearance").get_to(appearance);
        archive.at("portrait_id").get_to(portrait_id);
        archive.at("hp").get_to(hp);
        archive.at("hp_current").get_to(hp_current);
        archive.at("hardness").get_to(hardness);
        archive.at("interruptable").get_to(interruptable);
        archive.at("plot").get_to(plot);
        archive.at("description").get_to(description);
        archive.at("conversation").get_to(conversation);
        archive.at("saves").get_to(saves);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Placeable::from_json exception {}", e.what());
        return false;
    }
    return true;
}

GffOutputArchive Placeable::to_gff(SerializationProfile profile) const
{
    GffOutputArchive archive{"UTP"};

    archive.top.add_fields({
        {"TemplateResRef", common_.resref},
        {"LocName", common_.name},
        {"Tag", common_.tag},
        {"Faction", common_.faction},
    });

    if (profile == SerializationProfile::blueprint) {
        archive.top.add_field("Comment", common_.comment);
        archive.top.add_field("PaletteID", common_.palette_id);
    } else {
        archive.top.add_fields({
            {"PositionX", common_.location.position.x},
            {"PositionY", common_.location.position.y},
            {"PositionZ", common_.location.position.z},
            {"OrientationX", common_.location.orientation.x},
            {"OrientationY", common_.location.orientation.y},
        });
    }

    if (common_.local_data.size()) {
        common_.local_data.to_gff(archive.top);
    }

    lock.to_gff(archive.top);
    trap.to_gff(archive.top);

    scripts.to_gff(archive.top);
    if (has_inventory) {
        inventory.to_gff(archive.top, profile);
    }

    uint8_t type = 0;
    uint8_t anim = static_cast<uint8_t>(animation_state);
    archive.top.add_fields({
        {"Type", type}, // Obsolete, unused
        {"AnimationState", anim},
        {"BodyBag", bodybag},
        {"HasInventory", has_inventory},
        {"Static", static_},
        {"Useable", useable},
        {"Appearance", appearance},
        {"Conversation", conversation},
        {"Description", description},
        {"Hardness", hardness},
        {"HP", hp},
        {"CurrentHP", hp_current},
        {"Interruptable", interruptable},
        {"Plot", plot},
        {"PortraitId", portrait_id},
    });

    uint8_t save = static_cast<uint8_t>(saves.fort);
    archive.top.add_field("Fort", save);
    save = static_cast<uint8_t>(saves.reflex);
    archive.top.add_field("Ref", save);
    save = static_cast<uint8_t>(saves.will);
    archive.top.add_field("Will", save);

    archive.build();
    return archive;
}

nlohmann::json Placeable::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTP";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["common"] = common_.to_json(profile);
    j["scripts"] = scripts.to_json();

    j["inventory"] = inventory.to_json(profile);
    j["animation_state"] = animation_state;

    j["bodybag"] = bodybag;
    j["has_inventory"] = has_inventory;
    j["useable"] = useable;
    j["static"] = static_;

    j["appearance"] = appearance;
    j["portrait_id"] = portrait_id;
    j["hp"] = hp;
    j["hp_current"] = hp_current;
    j["hardness"] = hardness;
    j["interruptable"] = interruptable;
    j["plot"] = plot;

    j["description"] = description;
    j["conversation"] = conversation;
    j["saves"] = saves;
    j["lock"] = lock.to_json();
    j["trap"] = trap.to_json();

    return j;
}

} // namespace nw
