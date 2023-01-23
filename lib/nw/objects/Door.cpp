#include "Door.hpp"

#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// == DoorScripts =============================================================
// ============================================================================

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

// == Door ====================================================================
// ============================================================================

Door::Door()
{
    set_handle({object_invalid, ObjectType::door, 0});
}

bool Door::deserialize(Door* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    try {
        obj->common.from_json(archive.at("common"), profile, ObjectType::door);
        obj->lock.from_json(archive.at("lock"));
        obj->scripts.from_json(archive.at("scripts"));
        obj->trap.from_json(archive.at("trap"));

        archive.at("conversation").get_to(obj->conversation);
        archive.at("description").get_to(obj->description);
        archive.at("saves").get_to(obj->saves);

        archive.at("appearance").get_to(obj->appearance);
        archive.at("faction").get_to(obj->faction);
        archive.at("generic_type").get_to(obj->generic_type);

        archive.at("hp").get_to(obj->hp);
        archive.at("hp_current").get_to(obj->hp_current);
        archive.at("loadscreen").get_to(obj->loadscreen);
        archive.at("portrait_id").get_to(obj->portrait_id);

        archive.at("animation_state").get_to(obj->animation_state);
        archive.at("hardness").get_to(obj->hardness);
        archive.at("interruptable").get_to(obj->interruptable);
        archive.at("linked_to_flags").get_to(obj->linked_to_flags);
        archive.at("linked_to").get_to(obj->linked_to);
        archive.at("plot").get_to(obj->plot);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Door::to_json exception: {}", e.what());
        return false;
    }

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }
    return true;
}

bool Door::serialize(const Door* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTD";
    archive["$version"] = Door::json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::door);
    archive["lock"] = obj->lock.to_json();
    archive["scripts"] = obj->scripts.to_json();
    archive["trap"] = obj->trap.to_json();

    archive["conversation"] = obj->conversation;
    archive["description"] = obj->description;
    archive["linked_to"] = obj->linked_to;
    archive["saves"] = obj->saves;

    archive["appearance"] = obj->appearance;
    archive["faction"] = obj->faction;
    archive["generic_type"] = obj->generic_type;

    archive["hp"] = obj->hp;
    archive["hp_current"] = obj->hp_current;
    archive["loadscreen"] = obj->loadscreen;
    archive["portrait_id"] = obj->portrait_id;

    archive["animation_state"] = obj->animation_state;
    archive["hardness"] = obj->hardness;
    archive["interruptable"] = obj->interruptable;
    archive["linked_to_flags"] = obj->linked_to_flags;
    archive["plot"] = obj->plot;

    return true;
}

// == Door - Serialization - Gff ==============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(DoorScripts& self, const GffStruct& archive)
{
    archive.get_to("OnClick", self.on_click);
    archive.get_to("OnClosed", self.on_closed);
    archive.get_to("OnDamaged", self.on_damaged);
    archive.get_to("OnDeath", self.on_death);
    archive.get_to("OnDisarm", self.on_disarm);
    archive.get_to("OnHeartbeat", self.on_heartbeat);
    archive.get_to("OnLock", self.on_lock);
    archive.get_to("OnMeleeAttacked", self.on_melee_attacked);
    archive.get_to("OnOpen", self.on_open);
    archive.get_to("OnFailToOpen", self.on_open_failure);
    archive.get_to("OnSpellCastAt", self.on_spell_cast_at);
    archive.get_to("OnTrapTriggered", self.on_trap_triggered);
    archive.get_to("OnUnlock", self.on_unlock);
    archive.get_to("OnUserDefined", self.on_user_defined);

    return true;
}

bool serialize(const DoorScripts& self, GffBuilderStruct& archive)
{
    archive.add_field("OnClick", self.on_click)
        .add_field("OnClosed", self.on_closed)
        .add_field("OnDamaged", self.on_damaged)
        .add_field("OnDeath", self.on_death)
        .add_field("OnDisarm", self.on_disarm)
        .add_field("OnHeartbeat", self.on_heartbeat)
        .add_field("OnLock", self.on_lock)
        .add_field("OnMeleeAttacked", self.on_melee_attacked)
        .add_field("OnOpen", self.on_open)
        .add_field("OnFailToOpen", self.on_open_failure)
        .add_field("OnSpellCastAt", self.on_spell_cast_at)
        .add_field("OnTrapTriggered", self.on_trap_triggered)
        .add_field("OnUnlock", self.on_unlock)
        .add_field("OnUserDefined", self.on_user_defined);

    return true;
}

bool deserialize(Door* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    deserialize(obj->common, archive, profile, ObjectType::door);
    deserialize(obj->lock, archive);
    deserialize(obj->scripts, archive);
    deserialize(obj->trap, archive);

    bool result = true;
    uint8_t save;

    archive.get_to("Conversation", obj->conversation);
    archive.get_to("Description", obj->description);
    archive.get_to("LinkedTo", obj->linked_to);

    archive.get_to("Fort", save);
    obj->saves.fort = save;
    archive.get_to("Ref", save);
    obj->saves.reflex = save;
    archive.get_to("Will", save);
    obj->saves.will = save;

    archive.get_to("Appearance", obj->appearance);
    archive.get_to("Faction", obj->faction);
    if (!archive.get_to("GenericType_New", obj->generic_type, false)) {
        uint8_t temp = 0;
        archive.get_to("GenericType", temp);
        obj->generic_type = temp;
    }

    archive.get_to("HP", obj->hp);
    archive.get_to("CurrentHP", obj->hp_current);
    archive.get_to("LoadScreenID", obj->loadscreen);
    archive.get_to("PortraitId", obj->portrait_id);

    archive.get_to("AnimationState", obj->animation_state);
    archive.get_to("Hardness", obj->hardness);
    archive.get_to("Interruptable", obj->interruptable);
    archive.get_to("LinkedToFlags", obj->linked_to_flags);
    archive.get_to("Plot", obj->plot);

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }
    return result;
}

bool serialize(const Door* obj, GffBuilderStruct& archive, SerializationProfile profile)
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

    serialize(obj->lock, archive);
    serialize(obj->scripts, archive);
    serialize(obj->trap, archive);

    archive.add_field("Conversation", obj->conversation)
        .add_field("Description", obj->description)
        .add_field("LinkedTo", obj->linked_to);

    archive.add_field("Fort", static_cast<uint8_t>(obj->saves.fort))
        .add_field("Ref", static_cast<uint8_t>(obj->saves.reflex))
        .add_field("Will", static_cast<uint8_t>(obj->saves.will));

    archive.add_field("Appearance", obj->appearance)
        .add_field("Faction", obj->faction)
        .add_field("GenericType_New", obj->generic_type);

    archive.add_field("HP", obj->hp)
        .add_field("CurrentHP", obj->hp_current)
        .add_field("LoadScreenID", obj->loadscreen)
        .add_field("PortraitId", obj->portrait_id);

    archive.add_field("AnimationState", obj->animation_state)
        .add_field("Hardness", obj->hardness)
        .add_field("Interruptable", obj->interruptable)
        .add_field("LinkedToFlags", obj->linked_to_flags)
        .add_field("Plot", obj->plot);

    return true;
}

GffBuilder serialize(const Door* obj, SerializationProfile profile)
{
    GffBuilder out{"UTD"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    serialize(obj, out.top, profile);
    out.build();
    return out;
}
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
