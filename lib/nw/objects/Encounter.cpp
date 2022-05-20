#include "Encounter.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// -- EncounterScripts --------------------------------------------------------

bool EncounterScripts::from_gff(const GffInputArchiveStruct& archive)
{
    return archive.get_to("OnEntered", on_entered)
        && archive.get_to("OnExhausted", on_exhausted)
        && archive.get_to("OnExit", on_exit)
        && archive.get_to("OnHeartbeat", on_heartbeat)
        && archive.get_to("OnUserDefined", on_user_defined);
}

bool EncounterScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_entered").get_to(on_entered);
        archive.at("on_exhausted").get_to(on_exhausted);
        archive.at("on_exit").get_to(on_exit);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "EncounterScripts::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool EncounterScripts::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_field("OnEntered", on_entered)
        .add_field("OnExhausted", on_exhausted)
        .add_field("OnExit", on_exit)
        .add_field("OnHeartbeat", on_heartbeat)
        .add_field("OnUserDefined", on_user_defined);

    return true;
}

nlohmann::json EncounterScripts::to_json() const
{
    return {
        {"on_entered", on_entered},
        {"on_exhausted", on_exhausted},
        {"on_exit", on_exit},
        {"on_heartbeat", on_heartbeat},
        {"on_user_defined", on_user_defined}};
}

// -- SpawnCreature -----------------------------------------------------------

bool SpawnCreature::from_gff(const GffInputArchiveStruct& archive)
{
    return archive.get_to("Appearance", appearance)
        && archive.get_to("CR", cr)
        && archive.get_to("ResRef", resref)
        && archive.get_to("SingleSpawn", single_spawn);
}

bool SpawnCreature::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("appearance").get_to(appearance);
        archive.at("cr").get_to(cr);
        archive.at("resref").get_to(resref);
        archive.at("single_spawn").get_to(single_spawn);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }

    return true;
}

nlohmann::json SpawnCreature::to_json() const
{
    return {{"appearance", appearance},
        {"cr", cr},
        {"resref", resref},
        {"single_spawn", single_spawn}};
}

// -- SpawnPoint ---------------------------------------------------------------

bool SpawnPoint::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("Orientation", orientation);
    archive.get_to("X", position.x);
    archive.get_to("Y", position.y);
    archive.get_to("Z", position.z);

    return true;
}

bool SpawnPoint::from_json(const nlohmann::json& archive)
{
    archive.at("orientation").get_to(orientation);
    position.x = archive.at("position")[0].get<float>();
    position.y = archive.at("position")[1].get<float>();
    position.z = archive.at("position")[2].get<float>();
    return true;
}

nlohmann::json SpawnPoint::to_json() const
{
    return {{"position", {position.x, position.y, position.z}},
        {"orientation", orientation}};
}

// -- Encounter ---------------------------------------------------------------

bool Encounter::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto encounter = ent.get_mut<Encounter>();
    auto scripts = ent.get_mut<EncounterScripts>();
    auto common = ent.get_mut<Common>();

    if (!common->from_gff(archive, profile)) { return false; }
    scripts->from_gff(archive);

    size_t sz = archive["CreatureList"].size();
    encounter->creatures.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        encounter->creatures[i].from_gff(archive["CreatureList"][i]);
    }

    if (profile == SerializationProfile::instance) {
        sz = archive["Geometry"].size();
        encounter->geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("X", v.x);
            archive["Geometry"][i].get_to("Y", v.y);
            archive["Geometry"][i].get_to("Z", v.z);
            encounter->geometry.push_back(v);
        }

        sz = archive["SpawnPointList"].size();
        encounter->spawn_points.resize(sz);
        for (size_t i = 0; i < sz; ++i) {
            encounter->spawn_points[i].from_gff(archive["SpawnPointList"][i]);
        }
    }

    archive.get_to("Faction", encounter->faction);
    archive.get_to("MaxCreatures", encounter->creatures_max);
    archive.get_to("RecCreatures", encounter->creatures_recommended);
    archive.get_to("Difficulty", encounter->difficulty);
    archive.get_to("DifficultyIndex", encounter->difficulty_index);
    archive.get_to("ResetTime", encounter->reset_time);
    archive.get_to("Respawns", encounter->respawns);
    archive.get_to("SpawnOption", encounter->spawn_option);

    archive.get_to("Active", encounter->active);
    archive.get_to("PlayerOnly", encounter->player_only);
    archive.get_to("Reset", encounter->reset);

    return true;
}

bool Encounter::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto encounter = ent.get_mut<Encounter>();
    auto scripts = ent.get_mut<EncounterScripts>();
    auto common = ent.get_mut<Common>();

    common->from_json(archive.at("common"), profile);
    scripts->from_json(archive.at("scripts"));

    auto& arr = archive.at("creatures");
    encounter->creatures.resize(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        encounter->creatures[i].from_json(arr[i]);
    }

    if (profile == SerializationProfile::instance) {
        const auto& g = archive.at("geometry");
        for (const auto& geom : g) {
            encounter->geometry.emplace_back(geom[0].get<float>(), geom[1].get<float>(), geom[2].get<float>());
        }

        const auto& sp_list = archive.at("spawn_points");
        encounter->spawn_points.resize(sp_list.size());
        for (size_t i = 0; i < sp_list.size(); ++i) {
            encounter->spawn_points[i].from_json(sp_list[i]);
        }
    }

    archive.at("creatures_max").get_to(encounter->creatures_max);
    archive.at("creatures_recommended").get_to(encounter->creatures_recommended);
    archive.at("difficulty").get_to(encounter->difficulty);
    archive.at("difficulty_index").get_to(encounter->difficulty_index);
    archive.at("faction").get_to(encounter->faction);
    archive.at("reset_time").get_to(encounter->reset_time);
    archive.at("respawns").get_to(encounter->respawns);
    archive.at("spawn_option").get_to(encounter->spawn_option);

    archive.at("active").get_to(encounter->active);
    archive.at("player_only").get_to(encounter->player_only);
    archive.at("reset").get_to(encounter->reset);

    return true;
}

bool Encounter::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto encounter = ent.get<Encounter>();
    auto scripts = ent.get<EncounterScripts>();
    auto common = ent.get<Common>();

    archive.add_field("TemplateResRef", common->resref)
        .add_field("LocalizedName", common->name)
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

    scripts->to_gff(archive);

    auto& list = archive.add_list("CreatureList");
    for (const auto& c : encounter->creatures) {
        list.push_back(0)
            .add_field("Appearance", c.appearance)
            .add_field("CR", c.cr)
            .add_field("ResRef", c.resref)
            .add_field("SingleSpawn", c.single_spawn);
    }

    if (profile != SerializationProfile::blueprint) {
        auto& geo_list = archive.add_list("Geometry");
        for (const auto& g : encounter->geometry) {
            geo_list.push_back(1)
                .add_field("X", g.x)
                .add_field("Y", g.y)
                .add_field("Z", g.z);
        }

        auto& sp_list = archive.add_list("SpawnPointList");
        for (const auto& sp : encounter->spawn_points) {
            sp_list.push_back(0)
                .add_field("Orientation", sp.orientation)
                .add_field("X", sp.position.x)
                .add_field("Y", sp.position.y)
                .add_field("Z", sp.position.z);
        }
    }

    archive.add_field("MaxCreatures", encounter->creatures_max)
        .add_field("RecCreatures", encounter->creatures_recommended)
        .add_field("Difficulty", encounter->difficulty)
        .add_field("DifficultyIndex", encounter->difficulty_index)
        .add_field("Faction", encounter->faction)
        .add_field("ResetTime", encounter->reset_time)
        .add_field("Respawns", encounter->respawns)
        .add_field("SpawnOption", encounter->spawn_option);

    archive.add_field("Active", encounter->active)
        .add_field("PlayerOnly", encounter->player_only)
        .add_field("Reset", encounter->reset);

    return true;
}

GffOutputArchive Encounter::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTE"};
    Encounter::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Encounter::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto encounter = ent.get<Encounter>();
    auto scripts = ent.get<EncounterScripts>();
    auto common = ent.get<Common>();

    archive["$type"] = "UTE";
    archive["$version"] = json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["scripts"] = scripts->to_json();

    auto& arr = archive["creatures"] = nlohmann::json::array();
    for (const auto& c : encounter->creatures) {
        arr.push_back(c.to_json());
    }

    if (profile == SerializationProfile::instance) {
        auto& g = archive["geometry"] = nlohmann::json::array();
        for (const auto& geom : encounter->geometry) {
            g.push_back({geom.x, geom.y, geom.z});
        }

        auto& sp = archive["spawn_points"] = nlohmann::json::array();
        for (const auto& spawn : encounter->spawn_points) {
            sp.push_back(spawn.to_json());
        }
    }

    archive["creatures_max"] = encounter->creatures_max;
    archive["creatures_recommended"] = encounter->creatures_recommended;
    archive["difficulty"] = encounter->difficulty;
    archive["difficulty_index"] = encounter->difficulty_index;
    archive["faction"] = encounter->faction;
    archive["reset_time"] = encounter->reset_time;
    archive["respawns"] = encounter->respawns;
    archive["spawn_option"] = encounter->spawn_option;

    archive["active"] = encounter->active;
    archive["player_only"] = encounter->player_only;
    archive["reset"] = encounter->reset;

    return true;
}

} // namespace nw
