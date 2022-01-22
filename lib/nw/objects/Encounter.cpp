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
    archive.add_fields({
        {"OnEntered", on_entered},
        {"OnExhausted", on_exhausted},
        {"OnExit", on_exit},
        {"OnHeartbeat", on_heartbeat},
        {"OnUserDefined", on_user_defined},
    });

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

Encounter::Encounter()
    : common_{ObjectType::encounter}
{
}

Encounter::Encounter(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::encounter}
{
    valid_ = this->from_gff(archive, profile);
}

Encounter::Encounter(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::encounter}
{
    valid_ = this->from_json(archive, profile);
}

bool Encounter::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    if (!common_.from_gff(archive, profile)) { return false; }

    size_t sz = archive["CreatureList"].size();
    creatures.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        creatures[i].from_gff(archive["CreatureList"][i]);
    }

    archive.get_to("Faction", faction);
    archive.get_to("MaxCreatures", creatures_max);
    archive.get_to("RecCreatures", creatures_recommended);
    archive.get_to("Difficulty", difficulty);
    archive.get_to("DifficultyIndex", difficulty_index);
    archive.get_to("ResetTime", reset_time);
    archive.get_to("Respawns", respawns);
    archive.get_to("SpawnOption", spawn_option);

    archive.get_to("Active", active);
    archive.get_to("PlayerOnly", player_only);
    archive.get_to("Reset", reset);

    if (profile == SerializationProfile::instance) {
        size_t sz = archive["Geometry"].size();
        geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("X", v.x);
            archive["Geometry"][i].get_to("Y", v.y);
            archive["Geometry"][i].get_to("Z", v.z);
            geometry.push_back(v);
        }

        sz = archive["SpawnPointList"].size();
        spawn_points.resize(sz);
        for (size_t i = 0; i < sz; ++i) {
            spawn_points[i].from_gff(archive["SpawnPointList"][i]);
        }
    }
    return true;
}

bool Encounter::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    common_.from_json(archive.at("common"), profile);
    scripts.from_json(archive.at("scripts"));

    auto& arr = archive.at("creatures");
    creatures.resize(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        creatures[i].from_json(arr[i]);
    }

    archive.at("faction").get_to(faction);
    archive.at("creatures_max").get_to(creatures_max);
    archive.at("creatures_recommended").get_to(creatures_recommended);
    archive.at("difficulty").get_to(difficulty);
    archive.at("difficulty_index").get_to(difficulty_index);
    archive.at("reset_time").get_to(reset_time);
    archive.at("respawns").get_to(respawns);
    archive.at("spawn_option").get_to(spawn_option);

    archive.at("active").get_to(active);
    archive.at("player_only").get_to(player_only);
    archive.at("reset").get_to(reset);

    if (profile == SerializationProfile::instance) {
        const auto& g = archive.at("geometry");
        for (const auto& geom : g) {
            geometry.emplace_back(geom[0].get<float>(), geom[1].get<float>(), geom[2].get<float>());
        }

        const auto& sp_list = archive.at("spawn_points");
        spawn_points.resize(sp_list.size());
        for (size_t i = 0; i < sp_list.size(); ++i) {
            spawn_points[i].from_json(sp_list[i]);
        }
    }

    return true;
}

GffOutputArchive Encounter::to_gff(SerializationProfile profile) const
{
    GffOutputArchive archive{"UTE"};

    archive.top.add_fields({
        {"TemplateResRef", common_.resref},
        {"LocalizedName", common_.name},
        {"Tag", common_.tag},
        {"Faction", faction},
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

    scripts.to_gff(archive.top);

    auto& list = archive.top.add_list("CreatureList");
    for (const auto& c : creatures) {
        list.push_back(0, {
                              {"Appearance", c.appearance},
                              {"CR", c.cr},
                              {"ResRef", c.resref},
                              {"SingleSpawn", c.single_spawn},

                          });
    }

    archive.top.add_fields({
        {"MaxCreatures", creatures_max},
        {"RecCreatures", creatures_recommended},
        {"Difficulty", difficulty},
        {"DifficultyIndex", difficulty_index},
        {"ResetTime", reset_time},
        {"Respawns", respawns},
        {"SpawnOption", spawn_option},
        {"Active", active},
        {"PlayerOnly", player_only},
        {"Reset", reset},
    });

    if (profile != SerializationProfile::blueprint) {
        auto& geo_list = archive.top.add_list("Geometry");
        for (const auto& g : geometry) {
            geo_list.push_back(1, {
                                      {"X", g.x},
                                      {"Y", g.y},
                                      {"Z", g.z},
                                  });
        }

        auto& sp_list = archive.top.add_list("SpawnPointList");
        for (const auto& sp : spawn_points) {
            sp_list.push_back(0, {
                                     {"Orientation", sp.orientation},
                                     {"X", sp.position.x},
                                     {"Y", sp.position.y},
                                     {"Z", sp.position.z},
                                 });
        }
    }

    archive.build();
    return archive;
}

nlohmann::json Encounter::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTE";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["common"] = common_.to_json(profile);

    auto& arr = j["creatures"] = nlohmann::json::array();
    for (const auto& c : creatures) {
        arr.push_back(c.to_json());
    }

    j["scripts"] = scripts.to_json();

    j["faction"] = faction;
    j["creatures_max"] = creatures_max;
    j["creatures_recommended"] = creatures_recommended;
    j["difficulty"] = difficulty;
    j["difficulty_index"] = difficulty_index;
    j["reset_time"] = reset_time;
    j["respawns"] = respawns;
    j["spawn_option"] = spawn_option;

    j["active"] = active;
    j["player_only"] = player_only;
    j["reset"] = reset;

    if (profile == SerializationProfile::instance) {
        auto& g = j["geometry"] = nlohmann::json::array();
        for (const auto& geom : geometry) {
            g.push_back({geom.x, geom.y, geom.z});
        }

        auto& sp = j["spawn_points"] = nlohmann::json::array();
        for (const auto& spawn : spawn_points) {
            sp.push_back(spawn.to_json());
        }
    }

    return j;
}

} // namespace nw
