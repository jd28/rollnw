#include "Placeable.hpp"

#include <nlohmann/json.hpp>

namespace nw {

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
{
    set_handle({object_invalid, ObjectType::placeable, 0});
    inventory.owner = this;
}

bool Placeable::instantiate()
{
    if (instantiated_) return true;
    return instantiated_ = inventory.instantiate();
}

bool Placeable::deserialize(Placeable* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    try {
        obj->common.from_json(archive.at("common"), profile, ObjectType::placeable);
        obj->inventory.from_json(archive.at("inventory"), profile);
        obj->lock.from_json(archive.at("lock"));
        obj->scripts.from_json(archive.at("scripts"));
        obj->trap.from_json(archive.at("trap"));

        archive.at("conversation").get_to(obj->conversation);
        archive.at("description").get_to(obj->description);
        archive.at("saves").get_to(obj->saves);

        archive.at("appearance").get_to(obj->appearance);
        archive.at("faction").get_to(obj->faction);

        archive.at("hp").get_to(obj->hp);
        archive.at("hp_current").get_to(obj->hp_current);
        archive.at("portrait_id").get_to(obj->portrait_id);

        archive.at("animation_state").get_to(obj->animation_state);
        archive.at("bodybag").get_to(obj->bodybag);
        archive.at("has_inventory").get_to(obj->has_inventory);
        archive.at("hardness").get_to(obj->hardness);
        archive.at("interruptable").get_to(obj->interruptable);
        archive.at("plot").get_to(obj->plot);
        archive.at("static").get_to(obj->static_);
        archive.at("useable").get_to(obj->useable);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Placeable::from_json exception {}", e.what());
        return false;
    }

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool Placeable::serialize(const Placeable* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTP";
    archive["$version"] = json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::placeable);
    archive["inventory"] = obj->inventory.to_json(profile);
    archive["lock"] = obj->lock.to_json();
    archive["scripts"] = obj->scripts.to_json();
    archive["trap"] = obj->trap.to_json();

    archive["description"] = obj->description;
    archive["conversation"] = obj->conversation;
    archive["saves"] = obj->saves;

    archive["appearance"] = obj->appearance;
    archive["faction"] = obj->faction;

    archive["hp"] = obj->hp;
    archive["hp_current"] = obj->hp_current;
    archive["portrait_id"] = obj->portrait_id;

    archive["animation_state"] = obj->animation_state;
    archive["bodybag"] = obj->bodybag;
    archive["has_inventory"] = obj->has_inventory;
    archive["useable"] = obj->useable;
    archive["static"] = obj->static_;
    archive["hardness"] = obj->hardness;
    archive["interruptable"] = obj->interruptable;
    archive["plot"] = obj->plot;

    return true;
}

// == Placeable - Serialization - Gff =========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Placeable* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    deserialize(obj->common, archive, profile, ObjectType::placeable);

    archive.get_to("HasInventory", obj->has_inventory);
    if (obj->has_inventory) {
        deserialize(obj->inventory, archive, profile);
    }

    deserialize(obj->lock, archive);
    deserialize(obj->scripts, archive);
    deserialize(obj->trap, archive);
    deserialize(obj->inventory, archive, profile);

    archive.get_to("Conversation", obj->conversation);
    archive.get_to("Description", obj->description);
    uint8_t save;
    archive.get_to("Fort", save);
    obj->saves.fort = save;
    archive.get_to("Ref", save);
    obj->saves.reflex = save;
    archive.get_to("Will", save);
    obj->saves.will = save;

    archive.get_to("Appearance", obj->appearance);
    archive.get_to("Faction", obj->faction);

    archive.get_to("HP", obj->hp);
    archive.get_to("CurrentHP", obj->hp_current);
    archive.get_to("PortraitId", obj->portrait_id);

    archive.get_to("AnimationState", obj->animation_state);
    archive.get_to("BodyBag", obj->bodybag);
    archive.get_to("Hardness", obj->hardness);
    archive.get_to("Interruptable", obj->interruptable);
    archive.get_to("Plot", obj->plot);
    archive.get_to("Static", obj->static_);
    archive.get_to("Useable", obj->useable);

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool serialize(const Placeable* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("TemplateResRef", obj->common.resref)
        .add_field("LocName", obj->common.name)
        .add_field("Tag", obj->common.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj->common.comment);
        archive.add_field("PaletteID", obj->common.palette_id);
    } else {
        archive.add_field("PositionX", obj->common.location.position.x)
            .add_field("PositionY", obj->common.location.position.y)
            .add_field("PositionZ", obj->common.location.position.z)
            .add_field("OrientationX", obj->common.location.orientation.x)
            .add_field("OrientationY", obj->common.location.orientation.y);
    }

    if (obj->common.locals.size()) {
        serialize(obj->common.locals, archive, profile);
    }

    serialize(obj->inventory, archive, profile);
    serialize(obj->lock, archive);
    serialize(obj->scripts, archive);
    serialize(obj->trap, archive);

    archive.add_field("Conversation", obj->conversation)
        .add_field("Description", obj->description);

    archive.add_field("Fort", static_cast<uint8_t>(obj->saves.fort))
        .add_field("Ref", static_cast<uint8_t>(obj->saves.reflex))
        .add_field("Will", static_cast<uint8_t>(obj->saves.will));

    archive.add_field("Appearance", obj->appearance)
        .add_field("Faction", obj->faction);

    archive.add_field("HP", obj->hp)
        .add_field("CurrentHP", obj->hp_current)
        .add_field("PortraitId", obj->portrait_id);

    uint8_t type = 0;
    archive.add_field("Type", type) // Obsolete, unused
        .add_field("AnimationState", obj->animation_state)
        .add_field("BodyBag", obj->bodybag)
        .add_field("HasInventory", obj->has_inventory)
        .add_field("Hardness", obj->hardness)
        .add_field("Interruptable", obj->interruptable)
        .add_field("Plot", obj->plot)
        .add_field("Static", obj->static_)
        .add_field("Useable", obj->useable);

    return true;
}

GffBuilder serialize(const Placeable* obj, SerializationProfile profile)
{
    GffBuilder out{"UTP"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

bool deserialize(PlaceableScripts& self, const GffStruct& archive)
{
    archive.get_to("OnClick", self.on_click);
    archive.get_to("OnClosed", self.on_closed);
    archive.get_to("OnDamaged", self.on_damaged);
    archive.get_to("OnDeath", self.on_death);
    archive.get_to("OnDisarm", self.on_disarm);
    archive.get_to("OnHeartbeat", self.on_heartbeat);
    archive.get_to("OnInvDisturbed", self.on_inventory_disturbed);
    archive.get_to("OnLock", self.on_lock);
    archive.get_to("OnMeleeAttacked", self.on_melee_attacked);
    archive.get_to("OnOpen", self.on_open);
    archive.get_to("OnSpellCastAt", self.on_spell_cast_at);
    archive.get_to("OnTrapTriggered", self.on_trap_triggered);
    archive.get_to("OnUnlock", self.on_unlock);
    archive.get_to("OnUsed", self.on_used);
    archive.get_to("OnUserDefined", self.on_user_defined);

    return true;
}

bool serialize(const PlaceableScripts& self, GffBuilderStruct& archive)
{
    archive.add_field("OnClick", self.on_click)
        .add_field("OnClosed", self.on_closed)
        .add_field("OnDamaged", self.on_damaged)
        .add_field("OnDeath", self.on_death)
        .add_field("OnDisarm", self.on_disarm)
        .add_field("OnHeartbeat", self.on_heartbeat)
        .add_field("OnInvDisturbed", self.on_inventory_disturbed)
        .add_field("OnLock", self.on_lock)
        .add_field("OnMeleeAttacked", self.on_melee_attacked)
        .add_field("OnOpen", self.on_open)
        .add_field("OnSpellCastAt", self.on_spell_cast_at)
        .add_field("OnTrapTriggered", self.on_trap_triggered)
        .add_field("OnUnlock", self.on_unlock)
        .add_field("OnUsed", self.on_used)
        .add_field("OnUserDefined", self.on_user_defined);

    return true;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
