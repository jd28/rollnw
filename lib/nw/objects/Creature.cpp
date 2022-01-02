#include "Creature.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool CreatureScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("ScriptAttacked", on_attacked);
    archive.get_to("ScriptDamaged", on_damaged);
    archive.get_to("ScriptDeath", on_death);
    archive.get_to("ScriptDialogue", on_conversation);
    archive.get_to("ScriptDisturbed", on_disturbed);
    archive.get_to("ScriptEndRound", on_endround);
    archive.get_to("ScriptHeartbeat", on_heartbeat);
    archive.get_to("ScriptOnBlocked", on_blocked);
    archive.get_to("ScriptOnNotice", on_perceived);
    archive.get_to("ScriptRested", on_rested);
    archive.get_to("ScriptSpawn", on_spawn);
    archive.get_to("ScriptSpellAt", on_spell_cast_at);
    archive.get_to("ScriptUserDefine", on_user_defined);

    return true;
}

bool CreatureScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_attacked").get_to(on_attacked);
        archive.at("on_blocked").get_to(on_blocked);
        archive.at("on_conversation").get_to(on_conversation);
        archive.at("on_damaged").get_to(on_damaged);
        archive.at("on_death").get_to(on_death);
        archive.at("on_disturbed").get_to(on_disturbed);
        archive.at("on_endround").get_to(on_endround);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_perceived").get_to(on_perceived);
        archive.at("on_rested").get_to(on_rested);
        archive.at("on_spawn").get_to(on_spawn);
        archive.at("on_spell_cast_at").get_to(on_spell_cast_at);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool CreatureScripts::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_fields({
        {"ScriptAttacked", on_attacked},
        {"ScriptDamaged", on_damaged},
        {"ScriptDeath", on_death},
        {"ScriptDialogue", on_conversation},
        {"ScriptDisturbed", on_disturbed},
        {"ScriptEndRound", on_endround},
        {"ScriptHeartbeat", on_heartbeat},
        {"ScriptOnBlocked", on_blocked},
        {"ScriptOnNotice", on_perceived},
        {"ScriptRested", on_rested},
        {"ScriptSpawn", on_spawn},
        {"ScriptSpellAt", on_spell_cast_at},
        {"ScriptUserDefine", on_user_defined},
    });
    return true;
}

nlohmann::json CreatureScripts::to_json() const
{
    nlohmann::json j;

    j["on_attacked"] = on_attacked;
    j["on_blocked"] = on_blocked;
    j["on_conversation"] = on_conversation;
    j["on_damaged"] = on_damaged;
    j["on_death"] = on_death;
    j["on_disturbed"] = on_disturbed;
    j["on_endround"] = on_endround;
    j["on_heartbeat"] = on_heartbeat;
    j["on_perceived"] = on_perceived;
    j["on_rested"] = on_rested;
    j["on_spawn"] = on_spawn;
    j["on_spell_cast_at"] = on_spell_cast_at;
    j["on_user_defined"] = on_user_defined;

    return j;
}

Creature::Creature()
    : common_{ObjectType::creature}
    , inventory{this}
{
}
Creature::Creature(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::creature}
    , inventory{this}

{
    this->from_gff(archive, profile);
}

Creature::Creature(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::creature}
    , inventory{this}
{
    this->from_json(archive, profile);
}

bool Creature::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    appearance.from_gff(archive);
    combat_info.from_gff(archive);
    common_.from_gff(archive, profile);
    equipment.from_gff(archive, profile);
    inventory.from_gff(archive, profile);
    levels.from_gff(archive);
    scripts.from_gff(archive);
    stats.from_gff(archive);

    archive.get_to("FirstName", name_first);
    archive.get_to("LastName", name_last);
    archive.get_to("Conversation", conversation);
    archive.get_to("Deity", deity);
    archive.get_to("Description", description);
    archive.get_to("Subrace", subrace);

    archive.get_to("ChallengeRating", cr);
    archive.get_to("CRAdjust", cr_adjust);
    archive.get_to("DecayTime", decay_time);

    archive.get_to("CurrentHitPoints", hp_current);
    archive.get_to("HitPoints", hp);
    archive.get_to("MaxHitPoints", hp_max);
    archive.get_to("FactionID", faction_id);
    archive.get_to("SoundSetFile", soundset);
    archive.get_to("WalkRate", walkrate);

    archive.get_to("BodyBag", bodybag);
    archive.get_to("Disarmable", disarmable);
    archive.get_to("Gender", gender);
    archive.get_to("GoodEvil", good_evil);
    archive.get_to("IsImmortal", immortal);
    archive.get_to("Interruptable", interruptable);
    archive.get_to("LawfulChaotic", lawful_chaotic);
    archive.get_to("Lootable", lootable);
    archive.get_to("IsPC", pc);

    archive.get_to("NoPermDeath", chunk_death);
    archive.get_to("PerceptionRange", perception_range);
    archive.get_to("Plot", plot);
    archive.get_to("Race", race);
    archive.get_to("StartingPackage", starting_package);

    return true;
}

bool Creature::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        appearance.from_json(archive.at("appearance"));
        combat_info.from_json(archive.at("combat_info"));
        common_.from_json(archive.at("common"), profile);
        equipment.from_json(archive.at("equipment"), profile);
        inventory.from_json(archive.at("inventory"), profile);
        scripts.from_json(archive.at("scripts"));
        stats.from_json(archive.at("stats"));

        archive.at("bodybag").get_to(bodybag);
        archive.at("chunk_death").get_to(chunk_death);
        archive.at("conversation").get_to(conversation);
        archive.at("cr_adjust").get_to(cr_adjust);
        archive.at("cr").get_to(cr);
        archive.at("decay_time").get_to(decay_time);
        archive.at("deity").get_to(deity);
        archive.at("description").get_to(description);
        archive.at("disarmable").get_to(disarmable);
        archive.at("faction_id").get_to(faction_id);
        archive.at("gender").get_to(gender);
        archive.at("good_evil").get_to(good_evil);
        archive.at("hp_current").get_to(hp_current);
        archive.at("hp_max").get_to(hp_max);
        archive.at("hp").get_to(hp);
        archive.at("immortal").get_to(immortal);
        archive.at("interruptable").get_to(interruptable);
        archive.at("lawful_chaotic").get_to(lawful_chaotic);
        archive.at("lootable").get_to(lootable);
        archive.at("name_first").get_to(name_first);
        archive.at("name_last").get_to(name_last);
        archive.at("pc").get_to(pc);
        archive.at("perception_range").get_to(perception_range);
        archive.at("plot").get_to(plot);
        archive.at("race").get_to(race);
        archive.at("soundset").get_to(soundset);
        archive.at("starting_package").get_to(starting_package);
        archive.at("subrace").get_to(subrace);
        archive.at("walkrate").get_to(walkrate);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Creature::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    archive.add_fields({
        {"TemplateResRef", common_.resref},
        {"Tag", common_.tag},
    });

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("PaletteID", common_.palette_id);
    } else {
        archive.add_fields({
            {"PositionX", common_.location.position.x},
            {"PositionY", common_.location.position.y},
            {"PositionZ", common_.location.position.z},
            {"OrientationX", common_.location.orientation.x},
            {"OrientationY", common_.location.orientation.y},
        });
    }

    common_.local_data.to_gff(archive);

    appearance.to_gff(archive);
    combat_info.to_gff(archive);
    equipment.to_gff(archive, profile);
    inventory.to_gff(archive, profile);
    levels.to_gff(archive);
    scripts.to_gff(archive);
    stats.to_gff(archive);

    archive.add_list("TemplateList"); // Not sure if this is obsolete?

    archive.add_fields({
        {"FirstName", name_first},
        {"LastName", name_last},
        {"Conversation", conversation},
        {"Deity", deity},
        {"Description", description},
        {"Subrace", subrace},
        {"ChallengeRating", cr},
        {"CRAdjust", cr_adjust},
        {"DecayTime", decay_time},
        {"CurrentHitPoints", hp_current},
        {"HitPoints", hp},
        {"MaxHitPoints", hp_max},
        {"FactionID", faction_id},
        {"SoundSetFile", soundset},
        {"WalkRate", walkrate},
        {"BodyBag", bodybag},
        {"Disarmable", disarmable},
        {"Gender", gender},
        {"GoodEvil", good_evil},
        {"IsImmortal", immortal},
        {"Interruptable", interruptable},
        {"LawfulChaotic", lawful_chaotic},
        {"Lootable", lootable},
        {"IsPC", pc},
        {"NoPermDeath", chunk_death},
        {"PerceptionRange", perception_range},
        {"Plot", plot},
        {"Race", race},
        {"StartingPackage", starting_package},
    });

    return true;
}

nlohmann::json Creature::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    j["$type"] = "UTC";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["appearance"] = appearance.to_json();
    j["combat_info"] = combat_info.to_json();
    j["common"] = common_.to_json(profile);
    j["equipment"] = equipment.to_json(profile);
    j["inventory"] = inventory.to_json(profile);
    j["scripts"] = scripts.to_json();
    j["stats"] = stats.to_json();

    j["bodybag"] = bodybag;
    j["chunk_death"] = chunk_death;
    j["conversation"] = conversation;
    j["cr_adjust"] = cr_adjust;
    j["cr"] = cr;
    j["decay_time"] = decay_time;
    j["deity"] = deity;
    j["description"] = description;
    j["disarmable"] = disarmable;
    j["faction_id"] = faction_id;
    j["gender"] = gender;
    j["good_evil"] = good_evil;
    j["hp_current"] = hp_current;
    j["hp_max"] = hp_max;
    j["hp"] = hp;
    j["immortal"] = immortal;
    j["interruptable"] = interruptable;
    j["lawful_chaotic"] = lawful_chaotic;
    j["lootable"] = lootable;
    j["name_first"] = name_first;
    j["name_last"] = name_last;
    j["pc"] = pc;
    j["perception_range"] = perception_range;
    j["plot"] = plot;
    j["race"] = race;
    j["soundset"] = soundset;
    j["starting_package"] = starting_package;
    j["subrace"] = subrace;
    j["walkrate"] = walkrate;

    return j;
}

} // namespace nw
