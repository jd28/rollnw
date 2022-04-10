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

Door::Door()
    : common_{ObjectType::door}
{
}

Door::Door(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::door}
{
    valid_ = this->from_gff(archive, profile);
}

Door::Door(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::door}
{
    valid_ = this->from_json(archive, profile);
}

bool Door::valid() const noexcept { return valid_; }
Common* Door::common() { return &common_; }
const Common* Door::common() const { return &common_; }
Door* Door::as_door() { return this; }
const Door* Door::as_door() const { return this; }

bool Door::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    bool result = true;
    uint8_t save;

    common_.from_gff(archive, profile);
    archive.get_to("Conversation", conversation);
    archive.get_to("Description", description);
    archive.get_to("LinkedTo", linked_to);
    lock.from_gff(archive);

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
    if (!archive.get_to("GenericType_New", generic_type, false)) {
        uint8_t temp = 0;
        archive.get_to("GenericType", temp);
        generic_type = temp;
    }

    archive.get_to("HP", hp);
    archive.get_to("CurrentHP", hp_current);
    archive.get_to("LoadScreenID", loadscreen);
    archive.get_to("PortraitId", portrait_id);

    archive.get_to("AnimationState", animation_state);
    archive.get_to("Hardness", hardness);
    archive.get_to("Interruptable", interruptable);
    archive.get_to("LinkedToFlags", linked_to_flags);
    archive.get_to("Plot", plot);

    return result;
}

bool Door::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        common_.from_json(archive.at("common"), profile);
        archive.at("conversation").get_to(conversation);
        archive.at("description").get_to(description);
        lock.from_json(archive.at("lock"));
        archive.at("saves").get_to(saves);
        scripts.from_json(archive.at("scripts"));
        trap.from_json(archive.at("trap"));

        archive.at("appearance").get_to(appearance);
        archive.at("faction").get_to(faction);
        archive.at("generic_type").get_to(generic_type);

        archive.at("hp").get_to(hp);
        archive.at("hp_current").get_to(hp_current);
        archive.at("loadscreen").get_to(loadscreen);
        archive.at("portrait_id").get_to(portrait_id);

        archive.at("animation_state").get_to(animation_state);
        archive.at("hardness").get_to(hardness);
        archive.at("interruptable").get_to(interruptable);
        archive.at("linked_to_flags").get_to(linked_to_flags);
        archive.at("linked_to").get_to(linked_to);
        archive.at("plot").get_to(plot);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Door::to_json exception: {}", e.what());
        return valid_ = false;
    }

    return valid_ = true;
}

bool Door::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
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
        .add_field("Description", description)
        .add_field("LinkedTo", linked_to);

    lock.to_gff(archive);
    scripts.to_gff(archive);

    archive.add_field("Fort", static_cast<uint8_t>(saves.fort))
        .add_field("Ref", static_cast<uint8_t>(saves.reflex))
        .add_field("Will", static_cast<uint8_t>(saves.will));

    trap.to_gff(archive);

    archive.add_field("Appearance", appearance)
        .add_field("Faction", faction)
        .add_field("GenericType_New", generic_type);

    archive.add_field("HP", hp)
        .add_field("CurrentHP", hp_current)
        .add_field("LoadScreenID", loadscreen)
        .add_field("PortraitId", portrait_id);

    archive.add_field("AnimationState", animation_state)
        .add_field("Hardness", hardness)
        .add_field("Interruptable", interruptable)
        .add_field("LinkedToFlags", linked_to_flags)
        .add_field("Plot", plot);

    return true;
}

GffOutputArchive Door::to_gff(SerializationProfile profile) const
{
    GffOutputArchive out{"UTD"};
    this->to_gff(out.top, profile);
    out.build();
    return out;
}

nlohmann::json Door::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTD";
    j["$version"] = json_archive_version;

    j["common"] = common_.to_json(profile);
    j["conversation"] = conversation;
    j["description"] = description;
    j["linked_to"] = linked_to;
    j["lock"] = lock.to_json();
    j["saves"] = saves;
    j["scripts"] = scripts.to_json();
    j["trap"] = trap.to_json();

    j["appearance"] = appearance;
    j["faction"] = faction;
    j["generic_type"] = generic_type;

    j["hp"] = hp;
    j["hp_current"] = hp_current;
    j["loadscreen"] = loadscreen;
    j["portrait_id"] = portrait_id;

    j["animation_state"] = animation_state;
    j["hardness"] = hardness;
    j["interruptable"] = interruptable;
    j["linked_to_flags"] = linked_to_flags;
    j["plot"] = plot;

    return j;
}

} // namespace nw
