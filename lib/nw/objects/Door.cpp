#include "Door.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool DoorScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("OnClick", on_click);
    archive.get_to("OnClosed", on_closed);
    archive.get_to("OnDamaged", on_damaged);
    archive.get_to("OnDeath", on_death);
    archive.get_to("OnDisarm", on_disarm);
    archive.get_to("OnFailToOpen", on_open_failure);
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
        archive.at("on_open_failure").get_to(on_open_failure);
        archive.at("on_open").get_to(on_open);
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
    j["on_open_failure"] = on_open_failure;
    j["on_open"] = on_open;
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

bool Door::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    common_.from_gff(archive, profile);
    scripts.from_gff(archive);

    archive.get_to("AnimationState", animation_state);

    archive.get_to("GenericType", generic_type);
    archive.get_to("LinkedTo", linked_to);
    archive.get_to("LinkedToFlags", linked_to_flags);
    archive.get_to("LoadScreenID", loadscreen);
    trap.from_gff(archive);

    uint8_t save;
    // AnimationState
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

bool Door::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        common_.from_json(archive.at("common"), profile);
        scripts.from_json(archive.at("scripts"));

        archive.at("animation_state").get_to(animation_state);
        archive.at("linked_to").get_to(linked_to);
        archive.at("loadscreen").get_to(loadscreen);
        archive.at("generic_type").get_to(generic_type);
        archive.at("linked_to_flags").get_to(linked_to_flags);

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
        lock.from_json(archive.at("lock"));
        trap.from_json(archive.at("trap"));
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Door::to_json exception: {}", e.what());
        return valid_ = false;
    }

    return valid_ = true;
}

nlohmann::json Door::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTD";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["common"] = common_.to_json(profile);
    j["scripts"] = scripts.to_json();

    j["animation_state"] = animation_state;
    j["linked_to"] = linked_to;
    j["loadscreen"] = loadscreen;
    j["generic_type"] = generic_type;
    j["linked_to_flags"] = linked_to_flags;

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
