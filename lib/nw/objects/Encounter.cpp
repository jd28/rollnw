#include "Encounter.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// == EncounterScripts ========================================================
// ============================================================================

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

nlohmann::json EncounterScripts::to_json() const
{
    return {
        {"on_entered", on_entered},
        {"on_exhausted", on_exhausted},
        {"on_exit", on_exit},
        {"on_heartbeat", on_heartbeat},
        {"on_user_defined", on_user_defined}};
}

// == SpawnCreature ===========================================================
// ============================================================================

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
{
    set_handle({object_invalid, ObjectType::encounter, 0});
}

bool Encounter::deserialize(Encounter* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    obj->common.from_json(archive.at("common"), profile, ObjectType::encounter);
    obj->scripts.from_json(archive.at("scripts"));

    auto& arr = archive.at("creatures");
    obj->creatures.resize(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        obj->creatures[i].from_json(arr[i]);
    }

    if (profile == SerializationProfile::instance) {
        const auto& g = archive.at("geometry");
        for (const auto& geom : g) {
            obj->geometry.emplace_back(geom[0].get<float>(), geom[1].get<float>(), geom[2].get<float>());
        }

        const auto& sp_list = archive.at("spawn_points");
        obj->spawn_points.resize(sp_list.size());
        for (size_t i = 0; i < sp_list.size(); ++i) {
            obj->spawn_points[i].from_json(sp_list[i]);
        }
    }

    archive.at("creatures_max").get_to(obj->creatures_max);
    archive.at("creatures_recommended").get_to(obj->creatures_recommended);
    archive.at("difficulty").get_to(obj->difficulty);
    archive.at("difficulty_index").get_to(obj->difficulty_index);
    archive.at("faction").get_to(obj->faction);
    archive.at("reset_time").get_to(obj->reset_time);
    archive.at("respawns").get_to(obj->respawns);
    archive.at("spawn_option").get_to(obj->spawn_option);

    archive.at("active").get_to(obj->active);
    archive.at("player_only").get_to(obj->player_only);
    archive.at("reset").get_to(obj->reset);

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }
    return true;
}

bool Encounter::serialize(const Encounter* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTE";
    archive["$version"] = json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::encounter);
    archive["scripts"] = obj->scripts.to_json();

    auto& arr = archive["creatures"] = nlohmann::json::array();
    for (const auto& c : obj->creatures) {
        arr.push_back(c.to_json());
    }

    if (profile == SerializationProfile::instance) {
        auto& g = archive["geometry"] = nlohmann::json::array();
        for (const auto& geom : obj->geometry) {
            g.push_back({geom.x, geom.y, geom.z});
        }

        auto& sp = archive["spawn_points"] = nlohmann::json::array();
        for (const auto& spawn : obj->spawn_points) {
            sp.push_back(spawn.to_json());
        }
    }

    archive["creatures_max"] = obj->creatures_max;
    archive["creatures_recommended"] = obj->creatures_recommended;
    archive["difficulty"] = obj->difficulty;
    archive["difficulty_index"] = obj->difficulty_index;
    archive["faction"] = obj->faction;
    archive["reset_time"] = obj->reset_time;
    archive["respawns"] = obj->respawns;
    archive["spawn_option"] = obj->spawn_option;

    archive["active"] = obj->active;
    archive["player_only"] = obj->player_only;
    archive["reset"] = obj->reset;

    return true;
}

// == Encounter - Serialization - Gff =========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Encounter* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    if (!deserialize(obj->common, archive, profile, ObjectType::encounter)) {
        return false;
    }
    deserialize(obj->scripts, archive);

    size_t sz = archive["CreatureList"].size();
    obj->creatures.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        deserialize(obj->creatures[i], archive["CreatureList"][i]);
    }

    if (profile == SerializationProfile::instance) {
        sz = archive["Geometry"].size();
        obj->geometry.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            glm::vec3 v;
            archive["Geometry"][i].get_to("X", v.x);
            archive["Geometry"][i].get_to("Y", v.y);
            archive["Geometry"][i].get_to("Z", v.z);
            obj->geometry.push_back(v);
        }

        sz = archive["SpawnPointList"].size();
        obj->spawn_points.resize(sz);
        for (size_t i = 0; i < sz; ++i) {
            deserialize(obj->spawn_points[i], archive["SpawnPointList"][i]);
        }
    }

    archive.get_to("Faction", obj->faction);
    archive.get_to("MaxCreatures", obj->creatures_max);
    archive.get_to("RecCreatures", obj->creatures_recommended);
    archive.get_to("Difficulty", obj->difficulty);
    archive.get_to("DifficultyIndex", obj->difficulty_index);
    archive.get_to("ResetTime", obj->reset_time);
    archive.get_to("Respawns", obj->respawns);
    archive.get_to("SpawnOption", obj->spawn_option);

    archive.get_to("Active", obj->active);
    archive.get_to("PlayerOnly", obj->player_only);
    archive.get_to("Reset", obj->reset);

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }
    return true;
}

bool serialize(const Encounter* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("TemplateResRef", obj->common.resref)
        .add_field("LocalizedName", obj->common.name)
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

    serialize(obj->scripts, archive);

    auto& list = archive.add_list("CreatureList");
    for (const auto& c : obj->creatures) {
        list.push_back(0)
            .add_field("Appearance", c.appearance)
            .add_field("CR", c.cr)
            .add_field("ResRef", c.resref)
            .add_field("SingleSpawn", c.single_spawn);
    }

    if (profile != SerializationProfile::blueprint) {
        auto& geo_list = archive.add_list("Geometry");
        for (const auto& g : obj->geometry) {
            geo_list.push_back(1)
                .add_field("X", g.x)
                .add_field("Y", g.y)
                .add_field("Z", g.z);
        }

        auto& sp_list = archive.add_list("SpawnPointList");
        for (const auto& sp : obj->spawn_points) {
            sp_list.push_back(0)
                .add_field("Orientation", sp.orientation)
                .add_field("X", sp.position.x)
                .add_field("Y", sp.position.y)
                .add_field("Z", sp.position.z);
        }
    }

    archive.add_field("MaxCreatures", obj->creatures_max)
        .add_field("RecCreatures", obj->creatures_recommended)
        .add_field("Difficulty", obj->difficulty)
        .add_field("DifficultyIndex", obj->difficulty_index)
        .add_field("Faction", obj->faction)
        .add_field("ResetTime", obj->reset_time)
        .add_field("Respawns", obj->respawns)
        .add_field("SpawnOption", obj->spawn_option);

    archive.add_field("Active", obj->active)
        .add_field("PlayerOnly", obj->player_only)
        .add_field("Reset", obj->reset);

    return true;
}

GffBuilder serialize(const Encounter* obj, SerializationProfile profile)
{
    GffBuilder out{"UTE"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

bool deserialize(EncounterScripts& self, const GffStruct& archive)
{
    return archive.get_to("OnEntered", self.on_entered)
        && archive.get_to("OnExhausted", self.on_exhausted)
        && archive.get_to("OnExit", self.on_exit)
        && archive.get_to("OnHeartbeat", self.on_heartbeat)
        && archive.get_to("OnUserDefined", self.on_user_defined);
}

bool serialize(const EncounterScripts& self, GffBuilderStruct& archive)
{
    archive.add_field("OnEntered", self.on_entered)
        .add_field("OnExhausted", self.on_exhausted)
        .add_field("OnExit", self.on_exit)
        .add_field("OnHeartbeat", self.on_heartbeat)
        .add_field("OnUserDefined", self.on_user_defined);

    return true;
}

bool deserialize(SpawnCreature& self, const GffStruct& archive)
{
    return archive.get_to("Appearance", self.appearance)
        && archive.get_to("CR", self.cr)
        && archive.get_to("ResRef", self.resref)
        && archive.get_to("SingleSpawn", self.single_spawn);
}

bool deserialize(SpawnPoint& self, const GffStruct& archive)
{
    archive.get_to("Orientation", self.orientation);
    archive.get_to("X", self.position.x);
    archive.get_to("Y", self.position.y);
    archive.get_to("Z", self.position.z);

    return true;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
