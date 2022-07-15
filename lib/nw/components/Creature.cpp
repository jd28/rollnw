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
    archive.add_field("ScriptAttacked", on_attacked)
        .add_field("ScriptDamaged", on_damaged)
        .add_field("ScriptDeath", on_death)
        .add_field("ScriptDialogue", on_conversation)
        .add_field("ScriptDisturbed", on_disturbed)
        .add_field("ScriptEndRound", on_endround)
        .add_field("ScriptHeartbeat", on_heartbeat)
        .add_field("ScriptOnBlocked", on_blocked)
        .add_field("ScriptOnNotice", on_perceived)
        .add_field("ScriptRested", on_rested)
        .add_field("ScriptSpawn", on_spawn)
        .add_field("ScriptSpellAt", on_spell_cast_at)
        .add_field("ScriptUserDefine", on_user_defined);

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

bool Creature::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto cre = ent.get_mut<Creature>();
    auto common = ent.get_mut<Common>();
    auto appearance = ent.get_mut<Appearance>();
    auto combat_info = ent.get_mut<CombatInfo>();
    auto equipment = ent.get_mut<Equips>();
    auto inventory = ent.get_mut<Inventory>();
    auto levels = ent.get_mut<LevelStats>();
    auto scripts = ent.get_mut<CreatureScripts>();
    auto stats = ent.get_mut<CreatureStats>();

    common->from_gff(archive, profile);
    appearance->from_gff(archive);
    combat_info->from_gff(archive);
    equipment->from_gff(archive, profile);
    inventory->from_gff(archive, profile);
    levels->from_gff(archive);
    scripts->from_gff(archive);
    stats->from_gff(archive);

    archive.get_to("Conversation", cre->conversation);
    archive.get_to("Deity", cre->deity);
    archive.get_to("Description", cre->description);
    archive.get_to("FirstName", cre->name_first);
    archive.get_to("LastName", cre->name_last);
    archive.get_to("Subrace", cre->subrace);

    archive.get_to("ChallengeRating", cre->cr);
    archive.get_to("CRAdjust", cre->cr_adjust);
    archive.get_to("DecayTime", cre->decay_time);
    archive.get_to("WalkRate", cre->walkrate);

    archive.get_to("FactionID", cre->faction_id);
    archive.get_to("CurrentHitPoints", cre->hp_current);
    archive.get_to("HitPoints", cre->hp);
    archive.get_to("MaxHitPoints", cre->hp_max);
    archive.get_to("SoundSetFile", cre->soundset);

    archive.get_to("BodyBag", cre->bodybag);
    archive.get_to("NoPermDeath", cre->chunk_death);
    archive.get_to("Disarmable", cre->disarmable);
    archive.get_to("Gender", cre->gender);
    archive.get_to("GoodEvil", cre->good_evil);
    archive.get_to("IsImmortal", cre->immortal);
    archive.get_to("Interruptable", cre->interruptable);
    archive.get_to("LawfulChaotic", cre->lawful_chaotic);
    archive.get_to("Lootable", cre->lootable);
    archive.get_to("IsPC", cre->pc);
    archive.get_to("PerceptionRange", cre->perception_range);
    archive.get_to("Plot", cre->plot);

    uint8_t temp;
    if (archive.get_to("Race", temp)) {
        cre->race = make_race(temp);
    }

    archive.get_to("StartingPackage", cre->starting_package);

    return true;
}

bool Creature::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto cre = ent.get_mut<Creature>();
    auto common = ent.get_mut<Common>();
    auto appearance = ent.get_mut<Appearance>();
    auto combat_info = ent.get_mut<CombatInfo>();
    auto equipment = ent.get_mut<Equips>();
    auto inventory = ent.get_mut<Inventory>();
    auto levels = ent.get_mut<LevelStats>();
    auto scripts = ent.get_mut<CreatureScripts>();
    auto stats = ent.get_mut<CreatureStats>();

    try {
        common->from_json(archive.at("common"), profile);
        appearance->from_json(archive.at("appearance"));
        combat_info->from_json(archive.at("combat_info"));
        equipment->from_json(archive.at("equipment"), profile);
        inventory->from_json(archive.at("inventory"), profile);
        levels->from_json(archive.at("levels"));
        scripts->from_json(archive.at("scripts"));
        stats->from_json(archive.at("stats"));

        archive.at("conversation").get_to(cre->conversation);
        archive.at("description").get_to(cre->description);
        archive.at("name_first").get_to(cre->name_first);
        archive.at("name_last").get_to(cre->name_last);
        archive.at("subrace").get_to(cre->subrace);

        archive.at("cr").get_to(cre->cr);
        archive.at("cr_adjust").get_to(cre->cr_adjust);
        archive.at("decay_time").get_to(cre->decay_time);
        archive.at("walkrate").get_to(cre->walkrate);

        archive.at("faction_id").get_to(cre->faction_id);
        archive.at("hp").get_to(cre->hp);
        archive.at("hp_current").get_to(cre->hp_current);
        archive.at("hp_max").get_to(cre->hp_max);

        archive.at("bodybag").get_to(cre->bodybag);
        archive.at("chunk_death").get_to(cre->chunk_death);
        archive.at("deity").get_to(cre->deity);
        archive.at("disarmable").get_to(cre->disarmable);
        archive.at("gender").get_to(cre->gender);
        archive.at("good_evil").get_to(cre->good_evil);
        archive.at("immortal").get_to(cre->immortal);
        archive.at("interruptable").get_to(cre->interruptable);
        archive.at("lawful_chaotic").get_to(cre->lawful_chaotic);
        archive.at("lootable").get_to(cre->lootable);
        archive.at("pc").get_to(cre->pc);
        archive.at("perception_range").get_to(cre->perception_range);
        archive.at("plot").get_to(cre->plot);
        archive.at("race").get_to(cre->race);
        archive.at("soundset").get_to(cre->soundset);
        archive.at("starting_package").get_to(cre->starting_package);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Creature::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto cre = ent.get<Creature>();
    auto common = ent.get<Common>();
    auto appearance = ent.get<Appearance>();
    auto combat_info = ent.get<CombatInfo>();
    auto equipment = ent.get<Equips>();
    auto inventory = ent.get<Inventory>();
    auto levels = ent.get<LevelStats>();
    auto scripts = ent.get<CreatureScripts>();
    auto stats = ent.get<CreatureStats>();

    archive.add_field("TemplateResRef", common->resref)
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

    common->locals.to_gff(archive);
    appearance->to_gff(archive);
    combat_info->to_gff(archive);
    equipment->to_gff(archive, profile);
    inventory->to_gff(archive, profile);
    levels->to_gff(archive);
    scripts->to_gff(archive);
    stats->to_gff(archive);

    archive.add_field("Conversation", cre->conversation);
    archive.add_field("Deity", cre->deity);
    archive.add_field("Description", cre->description);
    archive.add_field("FirstName", cre->name_first);
    archive.add_field("LastName", cre->name_last);
    archive.add_field("Subrace", cre->subrace);

    archive.add_list("TemplateList"); // Not sure if this is obsolete?

    archive.add_field("ChallengeRating", cre->cr)
        .add_field("CRAdjust", cre->cr_adjust)
        .add_field("DecayTime", cre->decay_time)
        .add_field("WalkRate", cre->walkrate);

    archive.add_field("HitPoints", cre->hp)
        .add_field("CurrentHitPoints", cre->hp_current)
        .add_field("MaxHitPoints", cre->hp_max)
        .add_field("FactionID", cre->faction_id)
        .add_field("SoundSetFile", cre->soundset);

    archive.add_field("BodyBag", cre->bodybag)
        .add_field("Disarmable", cre->disarmable)
        .add_field("Gender", cre->gender)
        .add_field("GoodEvil", cre->good_evil)
        .add_field("IsImmortal", cre->immortal)
        .add_field("Interruptable", cre->interruptable)
        .add_field("LawfulChaotic", cre->lawful_chaotic)
        .add_field("Lootable", cre->lootable)
        .add_field("IsPC", cre->pc)
        .add_field("NoPermDeath", cre->chunk_death)
        .add_field("PerceptionRange", cre->perception_range)
        .add_field("Plot", cre->plot)
        .add_field("Race", uint8_t(cre->race))
        .add_field("StartingPackage", cre->starting_package);

    return true;
}

GffOutputArchive Creature::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTC"};
    Creature::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Creature::serialize(const flecs::entity ent, nlohmann::json& archive,
    SerializationProfile profile)
{
    auto cre = ent.get<Creature>();
    auto common = ent.get<Common>();
    auto appearance = ent.get<Appearance>();
    auto combat_info = ent.get<CombatInfo>();
    auto equipment = ent.get<Equips>();
    auto inventory = ent.get<Inventory>();
    auto levels = ent.get<LevelStats>();
    auto scripts = ent.get<CreatureScripts>();
    auto stats = ent.get<CreatureStats>();

    archive["$type"] = "UTC";
    archive["$version"] = json_archive_version;

    archive["appearance"] = appearance->to_json();
    archive["combat_info"] = combat_info->to_json();
    archive["common"] = common->to_json(profile);
    archive["equipment"] = equipment->to_json(profile);
    archive["inventory"] = inventory->to_json(profile);
    archive["levels"] = levels->to_json();
    archive["scripts"] = scripts->to_json();
    archive["stats"] = stats->to_json();

    archive["bodybag"] = cre->bodybag;
    archive["chunk_death"] = cre->chunk_death;
    archive["conversation"] = cre->conversation;
    archive["cr_adjust"] = cre->cr_adjust;
    archive["cr"] = cre->cr;
    archive["decay_time"] = cre->decay_time;
    archive["deity"] = cre->deity;
    archive["description"] = cre->description;
    archive["disarmable"] = cre->disarmable;
    archive["faction_id"] = cre->faction_id;
    archive["gender"] = cre->gender;
    archive["good_evil"] = cre->good_evil;
    archive["hp_current"] = cre->hp_current;
    archive["hp_max"] = cre->hp_max;
    archive["hp"] = cre->hp;
    archive["immortal"] = cre->immortal;
    archive["interruptable"] = cre->interruptable;
    archive["lawful_chaotic"] = cre->lawful_chaotic;
    archive["lootable"] = cre->lootable;
    archive["name_first"] = cre->name_first;
    archive["name_last"] = cre->name_last;
    archive["pc"] = cre->pc;
    archive["perception_range"] = cre->perception_range;
    archive["plot"] = cre->plot;
    archive["race"] = cre->race;
    archive["soundset"] = cre->soundset;
    archive["starting_package"] = cre->starting_package;
    archive["subrace"] = cre->subrace;
    archive["walkrate"] = cre->walkrate;

    return true;
}

} // namespace nw
