#include "Area.hpp"

#include "../kernel/Kernel.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// -- AreaScripts -------------------------------------------------------------

bool AreaScripts::from_gff(const GffInputArchiveStruct& archive)
{
    return archive.get_to("OnEnter", on_enter)
        && archive.get_to("OnExit", on_exit)
        && archive.get_to("OnHeartbeat", on_heartbeat)
        && archive.get_to("OnUserDefined", on_user_defined);
}

bool AreaScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_enter").get_to(on_enter);
        archive.at("on_exit").get_to(on_exit);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json AreaScripts::to_json() const
{
    nlohmann::json j;
    j["on_enter"] = on_enter;
    j["on_exit"] = on_exit;
    j["on_heartbeat"] = on_heartbeat;
    j["on_user_defined"] = on_user_defined;
    return j;
}

// -- AreaWeather -------------------------------------------------------------

bool AreaWeather::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("ChanceLightning", chance_lightning);
    archive.get_to("ChanceRain", chance_rain);
    archive.get_to("ChanceSnow", chance_snow);
    archive.get_to("MoonAmbientColor", color_moon_ambient);
    archive.get_to("MoonDiffuseColor", color_moon_diffuse);
    archive.get_to("MoonFogColor", color_moon_fog);
    archive.get_to("SunAmbientColor", color_sun_ambient);
    archive.get_to("SunDiffuseColor", color_sun_diffuse);
    archive.get_to("SunFogColor", color_sun_fog);
    archive.get_to("FogClipDist", fog_clip_distance);
    archive.get_to("WindPower", wind_power);

    archive.get_to("DayNightCycle", day_night_cycle);
    archive.get_to("IsNight", is_night);
    archive.get_to("LightingScheme", lighting_scheme);
    archive.get_to("MoonFogAmount", fog_moon_amount);
    archive.get_to("MoonShadows", moon_shadows);
    archive.get_to("SunFogAmount", fog_sun_amount);
    archive.get_to("SunShadows", sun_shadows);

    return true;
}

bool AreaWeather::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("chance_lightning").get_to(chance_lightning);
        archive.at("chance_rain").get_to(chance_rain);
        archive.at("chance_snow").get_to(chance_snow);
        archive.at("color_moon_ambient").get_to(color_moon_ambient);
        archive.at("color_moon_diffuse").get_to(color_moon_diffuse);
        archive.at("color_moon_fog").get_to(color_moon_fog);
        archive.at("color_sun_ambient").get_to(color_sun_ambient);
        archive.at("color_sun_diffuse").get_to(color_sun_diffuse);
        archive.at("color_sun_fog").get_to(color_sun_fog);
        archive.at("day_night_cycle").get_to(day_night_cycle);
        archive.at("fog_clip_distance").get_to(fog_clip_distance);
        archive.at("fog_moon_amount").get_to(fog_moon_amount);
        archive.at("fog_sun_amount").get_to(fog_sun_amount);
        archive.at("is_night").get_to(is_night);
        archive.at("lighting_scheme").get_to(lighting_scheme);
        archive.at("moon_shadows").get_to(moon_shadows);
        archive.at("sun_shadows").get_to(sun_shadows);
        archive.at("wind_power").get_to(wind_power);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json AreaWeather::to_json() const
{
    nlohmann::json j;

    j["chance_lightning"] = chance_lightning;
    j["chance_rain"] = chance_rain;
    j["chance_snow"] = chance_snow;
    j["color_moon_ambient"] = color_moon_ambient;
    j["color_moon_diffuse"] = color_moon_diffuse;
    j["color_moon_fog"] = color_moon_fog;
    j["color_sun_ambient"] = color_sun_ambient;
    j["color_sun_diffuse"] = color_sun_diffuse;
    j["color_sun_fog"] = color_sun_fog;
    j["day_night_cycle"] = day_night_cycle;
    j["fog_clip_distance"] = fog_clip_distance;
    j["fog_moon_amount"] = fog_moon_amount;
    j["fog_sun_amount"] = fog_sun_amount;
    j["is_night"] = is_night;
    j["lighting_scheme"] = lighting_scheme;
    j["moon_shadows"] = moon_shadows;
    j["sun_shadows"] = sun_shadows;
    j["wind_power"] = wind_power;

    return j;
}

// -- Tile --------------------------------------------------------------------

bool Tile::from_gff(const GffInputArchiveStruct& archive)
{
    return archive.get_to("Tile_ID", id)
        && archive.get_to("Tile_Height", height)
        && archive.get_to("Tile_Orientation", orientation)
        && archive.get_to("Tile_AnimLoop1", animloop1)
        && archive.get_to("Tile_AnimLoop2", animloop2)
        && archive.get_to("Tile_AnimLoop3", animloop3)
        && archive.get_to("Tile_MainLight1", mainlight1)
        && archive.get_to("Tile_MainLight2", mainlight2)
        && archive.get_to("Tile_SrcLight1", srclight1)
        && archive.get_to("Tile_SrcLight2", srclight2);
}

bool Tile::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("id").get_to(id);
        archive.at("height").get_to(height);
        archive.at("orientation").get_to(orientation);
        archive.at("animloop1").get_to(animloop1);
        archive.at("animloop2").get_to(animloop2);
        archive.at("animloop3").get_to(animloop3);
        archive.at("mainlight1").get_to(mainlight1);
        archive.at("mainlight2").get_to(mainlight2);
        archive.at("srclight1").get_to(srclight1);
        archive.at("srclight2").get_to(srclight2);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json Tile::to_json() const
{
    nlohmann::json j;
    j["id"] = id;
    j["height"] = height;
    j["orientation"] = orientation;
    j["animloop1"] = animloop1;
    j["animloop2"] = animloop2;
    j["animloop3"] = animloop3;
    j["mainlight1"] = mainlight1;
    j["mainlight2"] = mainlight2;
    j["srclight1"] = srclight1;
    j["srclight2"] = srclight2;
    return j;
}

// -- Area --------------------------------------------------------------------

bool Area::deserialize(flecs::entity ent, const GffInputArchiveStruct& are, const GffInputArchiveStruct& git, const GffInputArchiveStruct& gic)
{
    // [TODO] Load this..
    ROLLNW_UNUSED(gic);

    auto area = ent.get_mut<Area>();
    auto common = ent.get_mut<Common>();
    auto scripts = ent.get_mut<AreaScripts>();
    auto weather = ent.get_mut<AreaWeather>();

    common->from_gff(are, SerializationProfile::any);
    scripts->from_gff(are);
    weather->from_gff(are);

#define GIT_LIST(name, holder, type)                                               \
    do {                                                                           \
        auto st = git[name];                                                       \
        auto sz = st.size();                                                       \
        holder.reserve(sz);                                                        \
        for (size_t i = 0; i < sz; ++i) {                                          \
            auto oh = nw::kernel::objects().deserialize(type::object_type, st[i]); \
            if (oh.is_alive()) {                                                   \
                holder.push_back(oh);                                              \
            } else {                                                               \
                LOG_F(WARNING, "Something dreadfully wrong.");                     \
            }                                                                      \
        }                                                                          \
    } while (0)

    GIT_LIST("Creature List", area->creatures, Creature);
    GIT_LIST("Door List", area->doors, Door);
    GIT_LIST("Encounter List", area->encounters, Encounter);
    GIT_LIST("List", area->items, Item);
    GIT_LIST("Placeable List", area->placeables, Placeable);
    GIT_LIST("SoundList", area->sounds, Sound);
    GIT_LIST("StoreList", area->stores, Store);
    GIT_LIST("TriggerList", area->triggers, Trigger);
    GIT_LIST("WaypointList", area->waypoints, Waypoint);

#undef GIT_LIST

    are.get_to("Name", area->name);

    auto st = are["Tile_List"];
    size_t sz = st.size();
    area->tiles.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        area->tiles[i].from_gff(st[i]);
    }
    are.get_to("Tileset", area->tileset);

    are.get_to("Flags", area->flags);
    are.get_to("Height", area->height);
    are.get_to("ID", area->id);
    are.get_to("ModListenCheck", area->listen_check_mod);
    are.get_to("ModSpotCheck", area->spot_check_mod);
    are.get_to("Version", area->version);
    are.get_to("Width", area->width);

    are.get_to("LoadScreenID", area->loadscreen);

    are.get_to("NoRest", area->no_rest);
    are.get_to("PlayerVsPlayer", area->pvp);
    are.get_to("ShadowOpacity", area->shadow_opacity);
    are.get_to("SkyBox", area->skybox);

    return true;
}

bool Area::deserialize(flecs::entity ent, const nlohmann::json& caf)
{
    return Area::deserialize(ent, caf, caf, caf);
}

bool Area::deserialize(flecs::entity ent, const nlohmann::json& are,
    const nlohmann::json& git, const nlohmann::json& gic)
{
    // [TODO] Load this..
    ROLLNW_UNUSED(gic);

    auto area = ent.get_mut<Area>();
    auto common = ent.get_mut<Common>();
    auto scripts = ent.get_mut<AreaScripts>();
    auto weather = ent.get_mut<AreaWeather>();

    common->from_json(are.at("common"), SerializationProfile::any);
    scripts->from_json(are.at("scripts"));
    weather->from_json(are.at("weather"));

#define OBJECT_LIST_FROM_JSON(holder, type)                                         \
    do {                                                                            \
        auto& arr = git.at(#holder);                                                \
        size_t sz = arr.size();                                                     \
        holder.reserve(sz);                                                         \
        for (size_t i = 0; i < sz; ++i) {                                           \
            auto oh = nw::kernel::objects().deserialize(type::object_type, arr[i]); \
            if (oh.is_alive()) {                                                    \
                holder.push_back(oh);                                               \
            } else {                                                                \
                LOG_F(WARNING, "Something dreadfully wrong.");                      \
            }                                                                       \
        }                                                                           \
    } while (0)

    try {
        OBJECT_LIST_FROM_JSON(area->creatures, Creature);
        OBJECT_LIST_FROM_JSON(area->doors, Door);
        OBJECT_LIST_FROM_JSON(area->encounters, Encounter);
        OBJECT_LIST_FROM_JSON(area->items, Item);
        OBJECT_LIST_FROM_JSON(area->placeables, Placeable);
        OBJECT_LIST_FROM_JSON(area->sounds, Sound);
        OBJECT_LIST_FROM_JSON(area->stores, Store);
        OBJECT_LIST_FROM_JSON(area->triggers, Trigger);
        OBJECT_LIST_FROM_JSON(area->waypoints, Waypoint);

        are.at("comments").get_to(area->comments);
        are.at("name").get_to(area->name);

        auto& ts = are.at("tiles");
        area->tiles.resize(ts.size());
        for (size_t i = 0; i < ts.size(); ++i) {
            area->tiles[i].from_json(ts[i]);
        }
        are.at("tileset").get_to(area->tileset);

        are.at("creator_id").get_to(area->creator_id);
        are.at("flags").get_to(area->flags);
        are.at("height").get_to(area->height);
        are.at("id").get_to(area->id);
        are.at("listen_check_mod").get_to(area->listen_check_mod);
        are.at("spot_check_mod").get_to(area->spot_check_mod);
        are.at("version").get_to(area->version);
        are.at("width").get_to(area->width);

        are.at("loadscreen").get_to(area->loadscreen);

        are.at("no_rest").get_to(area->no_rest);
        are.at("pvp").get_to(area->pvp);
        are.at("shadow_opacity").get_to(area->shadow_opacity);
        are.at("skybox").get_to(area->skybox);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;

#undef OBJECT_LIST_FROM_JSON
}

#define OBJECT_LIST_TO_JSON(name)                                                 \
    do {                                                                          \
        auto& ref = archive[#name] = nlohmann::json::array();                     \
        for (const auto obj : name) {                                             \
            nlohmann::json j2;                                                    \
            kernel::objects().serialize(obj, j2, SerializationProfile::instance); \
            ref.push_back(j2);                                                    \
        }                                                                         \
    } while (0)

void Area::serialize(const flecs::entity ent, nlohmann::json& archive)
{
    auto area = ent.get<Area>();
    auto common = ent.get<Common>();
    auto scripts = ent.get<AreaScripts>();
    auto weather = ent.get<AreaWeather>();

    archive["$type"] = "CAF";
    archive["$version"] = Area::json_archive_version;

    archive["common"] = common->to_json(SerializationProfile::any);
    archive["weather"] = weather->to_json();
    archive["scripts"] = scripts->to_json();

    OBJECT_LIST_TO_JSON(area->creatures);
    OBJECT_LIST_TO_JSON(area->doors);
    OBJECT_LIST_TO_JSON(area->encounters);
    OBJECT_LIST_TO_JSON(area->items);
    OBJECT_LIST_TO_JSON(area->placeables);
    OBJECT_LIST_TO_JSON(area->sounds);
    OBJECT_LIST_TO_JSON(area->stores);
    OBJECT_LIST_TO_JSON(area->triggers);
    OBJECT_LIST_TO_JSON(area->waypoints);

    archive["comments"] = area->comments;
    archive["name"] = area->name;

    auto& ts = archive["tiles"] = nlohmann::json::array();
    for (const auto& tile : area->tiles) {
        ts.push_back(tile.to_json());
    }

    archive["tileset"] = area->tileset;

    archive["creator_id"] = area->creator_id;
    archive["flags"] = area->flags;
    archive["height"] = area->height;
    archive["id"] = area->id;
    archive["listen_check_mod"] = area->listen_check_mod;
    archive["spot_check_mod"] = area->spot_check_mod;
    archive["version"] = area->version;
    archive["width"] = area->width;

    archive["loadscreen"] = area->loadscreen;

    archive["no_rest"] = area->no_rest;
    archive["pvp"] = area->pvp;
    archive["shadow_opacity"] = area->shadow_opacity;
    archive["skybox"] = area->skybox;
}

#undef OBJECT_LIST_TO_JSON

} // namespace nw
