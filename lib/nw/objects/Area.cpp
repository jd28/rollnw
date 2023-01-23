#include "Area.hpp"

#include "../kernel/Objects.hpp"
#include "Creature.hpp"
#include "Door.hpp"
#include "Encounter.hpp"
#include "Item.hpp"
#include "Placeable.hpp"
#include "Sound.hpp"
#include "Store.hpp"
#include "Trigger.hpp"
#include "Waypoint.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// -- AreaScripts -------------------------------------------------------------

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

Area::Area()
{
    set_handle({object_invalid, ObjectType::area, 0});
}

bool Area::instantiate() { return true; }

bool Area::deserialize(Area* obj, const nlohmann::json& caf)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    return Area::deserialize(obj, caf, caf, caf);
}

bool Area::deserialize(Area* obj, const nlohmann::json& are,
    const nlohmann::json& git, const nlohmann::json& gic)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    // [TODO] Load this..
    ROLLNW_UNUSED(gic);

    obj->common.from_json(are.at("common"), SerializationProfile::any, ObjectType::area);
    obj->scripts.from_json(are.at("scripts"));
    obj->weather.from_json(are.at("weather"));

#define OBJECT_LIST_FROM_JSON(holder, type)                                            \
    do {                                                                               \
        auto& arr = git.at(#holder);                                                   \
        size_t sz = arr.size();                                                        \
        obj->holder.reserve(sz);                                                       \
        for (size_t i = 0; i < sz; ++i) {                                              \
            auto ob = nw::kernel::objects().make<type>();                              \
            if (ob && type::deserialize(ob, arr[i], SerializationProfile::instance)) { \
                obj->holder.push_back(ob);                                             \
                obj->holder.back()->instantiate();                                     \
            } else {                                                                   \
                LOG_F(WARNING, "Something dreadfully wrong.");                         \
            }                                                                          \
        }                                                                              \
    } while (0)

    try {
        OBJECT_LIST_FROM_JSON(creatures, Creature);
        OBJECT_LIST_FROM_JSON(doors, Door);
        OBJECT_LIST_FROM_JSON(encounters, Encounter);
        OBJECT_LIST_FROM_JSON(items, Item);
        OBJECT_LIST_FROM_JSON(placeables, Placeable);
        OBJECT_LIST_FROM_JSON(sounds, Sound);
        OBJECT_LIST_FROM_JSON(stores, Store);
        OBJECT_LIST_FROM_JSON(triggers, Trigger);
        OBJECT_LIST_FROM_JSON(waypoints, Waypoint);

        are.at("comments").get_to(obj->comments);
        are.at("name").get_to(obj->name);

        auto& ts = are.at("tiles");
        obj->tiles.resize(ts.size());
        for (size_t i = 0; i < ts.size(); ++i) {
            obj->tiles[i].from_json(ts[i]);
        }
        are.at("tileset").get_to(obj->tileset);

        are.at("creator_id").get_to(obj->creator_id);
        are.at("flags").get_to(obj->flags);
        are.at("height").get_to(obj->height);
        are.at("id").get_to(obj->id);
        are.at("listen_check_mod").get_to(obj->listen_check_mod);
        are.at("spot_check_mod").get_to(obj->spot_check_mod);
        are.at("version").get_to(obj->version);
        are.at("width").get_to(obj->width);

        are.at("loadscreen").get_to(obj->loadscreen);

        are.at("no_rest").get_to(obj->no_rest);
        are.at("pvp").get_to(obj->pvp);
        are.at("shadow_opacity").get_to(obj->shadow_opacity);
        are.at("skybox").get_to(obj->skybox);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;

#undef OBJECT_LIST_FROM_JSON
}

#define OBJECT_LIST_TO_JSON(name, type)                              \
    do {                                                             \
        auto& ref = archive[#name] = nlohmann::json::array();        \
        for (const auto ob : obj->name) {                            \
            nlohmann::json j2;                                       \
            type::serialize(ob, j2, SerializationProfile::instance); \
            ref.push_back(j2);                                       \
        }                                                            \
    } while (0)

void Area::serialize(const Area* obj, nlohmann::json& archive)
{
    if (!obj) return;

    archive["$type"] = "CAF";
    archive["$version"] = Area::json_archive_version;

    archive["common"] = obj->common.to_json(SerializationProfile::any, ObjectType::area);
    archive["weather"] = obj->weather.to_json();
    archive["scripts"] = obj->scripts.to_json();

    OBJECT_LIST_TO_JSON(creatures, Creature);
    OBJECT_LIST_TO_JSON(doors, Door);
    OBJECT_LIST_TO_JSON(encounters, Encounter);
    OBJECT_LIST_TO_JSON(items, Item);
    OBJECT_LIST_TO_JSON(placeables, Placeable);
    OBJECT_LIST_TO_JSON(sounds, Sound);
    OBJECT_LIST_TO_JSON(stores, Store);
    OBJECT_LIST_TO_JSON(triggers, Trigger);
    OBJECT_LIST_TO_JSON(waypoints, Waypoint);

    archive["comments"] = obj->comments;
    archive["name"] = obj->name;

    auto& ts = archive["tiles"] = nlohmann::json::array();
    for (const auto& tile : obj->tiles) {
        ts.push_back(tile.to_json());
    }

    archive["tileset"] = obj->tileset;

    archive["creator_id"] = obj->creator_id;
    archive["flags"] = obj->flags;
    archive["height"] = obj->height;
    archive["id"] = obj->id;
    archive["listen_check_mod"] = obj->listen_check_mod;
    archive["spot_check_mod"] = obj->spot_check_mod;
    archive["version"] = obj->version;
    archive["width"] = obj->width;

    archive["loadscreen"] = obj->loadscreen;

    archive["no_rest"] = obj->no_rest;
    archive["pvp"] = obj->pvp;
    archive["shadow_opacity"] = obj->shadow_opacity;
    archive["skybox"] = obj->skybox;
}

#undef OBJECT_LIST_TO_JSON

// == Area - Serialization - Gff ==============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(AreaScripts& self, const GffStruct& archive)
{
    return archive.get_to("OnEnter", self.on_enter)
        && archive.get_to("OnExit", self.on_exit)
        && archive.get_to("OnHeartbeat", self.on_heartbeat)
        && archive.get_to("OnUserDefined", self.on_user_defined);
}

bool deserialize(AreaWeather& self, const GffStruct& archive)
{
    archive.get_to("ChanceLightning", self.chance_lightning);
    archive.get_to("ChanceRain", self.chance_rain);
    archive.get_to("ChanceSnow", self.chance_snow);
    archive.get_to("MoonAmbientColor", self.color_moon_ambient);
    archive.get_to("MoonDiffuseColor", self.color_moon_diffuse);
    archive.get_to("MoonFogColor", self.color_moon_fog);
    archive.get_to("SunAmbientColor", self.color_sun_ambient);
    archive.get_to("SunDiffuseColor", self.color_sun_diffuse);
    archive.get_to("SunFogColor", self.color_sun_fog);
    archive.get_to("FogClipDist", self.fog_clip_distance);
    archive.get_to("WindPower", self.wind_power);

    archive.get_to("DayNightCycle", self.day_night_cycle);
    archive.get_to("IsNight", self.is_night);
    archive.get_to("LightingScheme", self.lighting_scheme);
    archive.get_to("MoonFogAmount", self.fog_moon_amount);
    archive.get_to("MoonShadows", self.moon_shadows);
    archive.get_to("SunFogAmount", self.fog_sun_amount);
    archive.get_to("SunShadows", self.sun_shadows);

    return true;
}

bool deserialize(Tile& self, const GffStruct& archive)
{
    return archive.get_to("Tile_ID", self.id)
        && archive.get_to("Tile_Height", self.height)
        && archive.get_to("Tile_Orientation", self.orientation)
        && archive.get_to("Tile_AnimLoop1", self.animloop1)
        && archive.get_to("Tile_AnimLoop2", self.animloop2)
        && archive.get_to("Tile_AnimLoop3", self.animloop3)
        && archive.get_to("Tile_MainLight1", self.mainlight1)
        && archive.get_to("Tile_MainLight2", self.mainlight2)
        && archive.get_to("Tile_SrcLight1", self.srclight1)
        && archive.get_to("Tile_SrcLight2", self.srclight2);
}

bool deserialize(Area* obj, const GffStruct& are, const GffStruct& git, const GffStruct& gic)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    // [TODO] Load this..
    ROLLNW_UNUSED(gic);

    deserialize(obj->common, are, SerializationProfile::any, ObjectType::area);
    deserialize(obj->scripts, are);
    deserialize(obj->weather, are);

#define GIT_LIST(name, holder, type)                                          \
    do {                                                                      \
        auto st = git[name];                                                  \
        auto sz = st.size();                                                  \
        holder.reserve(sz);                                                   \
        for (size_t i = 0; i < sz; ++i) {                                     \
            auto o = nw::kernel::objects().make<type>();                      \
            if (o && deserialize(o, st[i], SerializationProfile::instance)) { \
                holder.push_back(o);                                          \
                holder.back()->instantiate();                                 \
            } else {                                                          \
                LOG_F(WARNING, "Something dreadfully wrong.");                \
            }                                                                 \
        }                                                                     \
    } while (0)

    GIT_LIST("Creature List", obj->creatures, Creature);
    GIT_LIST("Door List", obj->doors, Door);
    GIT_LIST("Encounter List", obj->encounters, Encounter);
    GIT_LIST("List", obj->items, Item);
    GIT_LIST("Placeable List", obj->placeables, Placeable);
    GIT_LIST("SoundList", obj->sounds, Sound);
    GIT_LIST("StoreList", obj->stores, Store);
    GIT_LIST("TriggerList", obj->triggers, Trigger);
    GIT_LIST("WaypointList", obj->waypoints, Waypoint);

#undef GIT_LIST

    are.get_to("Name", obj->name);

    auto st = are["Tile_List"];
    size_t sz = st.size();
    obj->tiles.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        deserialize(obj->tiles[i], st[i]);
    }
    are.get_to("Tileset", obj->tileset);

    are.get_to("Flags", obj->flags);
    are.get_to("Height", obj->height);
    are.get_to("ID", obj->id);
    are.get_to("ModListenCheck", obj->listen_check_mod);
    are.get_to("ModSpotCheck", obj->spot_check_mod);
    are.get_to("Version", obj->version);
    are.get_to("Width", obj->width);

    are.get_to("LoadScreenID", obj->loadscreen);

    are.get_to("NoRest", obj->no_rest);
    are.get_to("PlayerVsPlayer", obj->pvp);
    are.get_to("ShadowOpacity", obj->shadow_opacity);
    are.get_to("SkyBox", obj->skybox);

    return true;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
