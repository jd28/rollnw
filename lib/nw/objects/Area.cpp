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

Area::Area(const GffInputArchiveStruct& caf, const GffInputArchiveStruct& gic)
    : Area(caf, caf, gic)
{
    is_caf_ = true;
}

Area::Area(const GffInputArchiveStruct& are, const GffInputArchiveStruct& git, const GffInputArchiveStruct& gic)
    : common_{ObjectType::area}
{
    valid_ = this->from_gff(are, git, gic);
}

Area::Area(const nlohmann::json& caf, const nlohmann::json& gic)
{
    valid_ = this->from_json(caf, gic);
}

bool Area::valid() const noexcept { return valid_; }
Common* Area::common() { return &common_; }
const Common* Area::common() const { return &common_; }
Area* Area::as_area() { return this; }
const Area* Area::as_area() const { return this; }

bool Area::from_gff(const GffInputArchiveStruct& are, const GffInputArchiveStruct& git, const GffInputArchiveStruct& gic)
{
    // [TODO] Load this..
    LIBNW_UNUSED(gic);

    common_.from_gff(are, SerializationProfile::any);

#define GIT_LIST(name, holder, type)                                        \
    do {                                                                    \
        auto st = git[name];                                                \
        auto sz = st.size();                                                \
        holder.reserve(sz);                                                 \
        for (size_t i = 0; i < sz; ++i) {                                   \
            auto oh = nw::kernel::objects().make(type::object_type, st[i]); \
            if (oh.is_alive()) {                                            \
                holder.push_back(oh);                                       \
            } else {                                                        \
                LOG_F(WARNING, "Something dreadfully wrong.");              \
            }                                                               \
        }                                                                   \
    } while (0)

    GIT_LIST("Creature List", creatures, Creature);
    GIT_LIST("Door List", doors, Door);
    GIT_LIST("Encounter List", encounters, Encounter);
    GIT_LIST("List", items, Item);
    GIT_LIST("Placeable List", placeables, Placeable);
    GIT_LIST("SoundList", sounds, Sound);
    GIT_LIST("StoreList", stores, Store);
    GIT_LIST("TriggerList", triggers, Trigger);
    GIT_LIST("WaypointList", waypoints, Waypoint);

#undef GIT_LIST

    are.get_to("Name", name);
    scripts.from_gff(are);

    auto st = are["Tile_List"];
    size_t sz = st.size();
    tiles.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        tiles[i].from_gff(st[i]);
    }
    are.get_to("Tileset", tileset);

    weather.from_gff(are);

    are.get_to("Flags", flags);
    are.get_to("Height", height);
    are.get_to("ID", id);
    are.get_to("ModListenCheck", listen_check_mod);
    are.get_to("ModSpotCheck", spot_check_mod);
    are.get_to("Version", version);
    are.get_to("Width", width);

    are.get_to("LoadScreenID", loadscreen);

    are.get_to("NoRest", no_rest);
    are.get_to("PlayerVsPlayer", pvp);
    are.get_to("ShadowOpacity", shadow_opacity);
    are.get_to("SkyBox", skybox);

    return true;
}

bool Area::from_json(const nlohmann::json& caf, const nlohmann::json& gic)
{
    // [TODO] Load this..
    LIBNW_UNUSED(gic);

#define OBJECT_LIST_FROM_JSON(holder, type)                                  \
    do {                                                                     \
        auto& arr = caf.at(#holder);                                         \
        size_t sz = arr.size();                                              \
        holder.reserve(sz);                                                  \
        for (size_t i = 0; i < sz; ++i) {                                    \
            auto oh = nw::kernel::objects().make(type::object_type, arr[i]); \
            if (oh.is_alive()) {                                             \
                holder.push_back(oh);                                        \
            } else {                                                         \
                LOG_F(WARNING, "Something dreadfully wrong.");               \
            }                                                                \
        }                                                                    \
    } while (0)

    try {
        common_.from_json(caf.at("common"), SerializationProfile::any);

        OBJECT_LIST_FROM_JSON(creatures, Creature);
        OBJECT_LIST_FROM_JSON(doors, Door);
        OBJECT_LIST_FROM_JSON(encounters, Encounter);
        OBJECT_LIST_FROM_JSON(items, Item);
        OBJECT_LIST_FROM_JSON(placeables, Placeable);
        OBJECT_LIST_FROM_JSON(sounds, Sound);
        OBJECT_LIST_FROM_JSON(stores, Store);
        OBJECT_LIST_FROM_JSON(triggers, Trigger);
        OBJECT_LIST_FROM_JSON(waypoints, Waypoint);

        caf.at("comments").get_to(comments);
        caf.at("name").get_to(name);
        scripts.from_json(caf.at("scripts"));

        auto& ts = caf.at("tiles");
        tiles.resize(ts.size());
        for (size_t i = 0; i < ts.size(); ++i) {
            tiles[i].from_json(ts[i]);
        }
        caf.at("tileset").get_to(tileset);

        weather.from_json(caf.at("weather"));

        caf.at("creator_id").get_to(creator_id);
        caf.at("flags").get_to(flags);
        caf.at("height").get_to(height);
        caf.at("id").get_to(id);
        caf.at("listen_check_mod").get_to(listen_check_mod);
        caf.at("spot_check_mod").get_to(spot_check_mod);
        caf.at("version").get_to(version);
        caf.at("width").get_to(width);

        caf.at("loadscreen").get_to(loadscreen);

        caf.at("no_rest").get_to(no_rest);
        caf.at("pvp").get_to(pvp);
        caf.at("shadow_opacity").get_to(shadow_opacity);
        caf.at("skybox").get_to(skybox);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;

#undef OBJECT_LIST_FROM_JSON
}

#define OBJECT_LIST_TO_JSON(name, type)                                              \
    do {                                                                             \
        auto& ref = j[#name] = nlohmann::json::array();                              \
        for (const auto obj : name) {                                                \
            ref.push_back(obj.get<type>()->to_json(SerializationProfile::instance)); \
        }                                                                            \
    } while (0)

nlohmann::json Area::to_json() const
{
    nlohmann::json j;
    j["$type"] = "CAF";
    j["$version"] = json_archive_version;

    j["common"] = common_.to_json(SerializationProfile::any);

    OBJECT_LIST_TO_JSON(creatures, Creature);
    OBJECT_LIST_TO_JSON(doors, Door);
    OBJECT_LIST_TO_JSON(encounters, Encounter);
    OBJECT_LIST_TO_JSON(items, Item);
    OBJECT_LIST_TO_JSON(placeables, Placeable);
    OBJECT_LIST_TO_JSON(sounds, Sound);
    OBJECT_LIST_TO_JSON(stores, Store);
    OBJECT_LIST_TO_JSON(triggers, Trigger);
    OBJECT_LIST_TO_JSON(waypoints, Waypoint);

    j["comments"] = comments;
    j["name"] = name;
    j["scripts"] = scripts.to_json();

    auto& ts = j["tiles"] = nlohmann::json::array();
    for (const auto& tile : tiles) {
        ts.push_back(tile.to_json());
    }

    j["tileset"] = tileset;
    j["weather"] = weather.to_json();

    j["creator_id"] = creator_id;
    j["flags"] = flags;
    j["height"] = height;
    j["id"] = id;
    j["listen_check_mod"] = listen_check_mod;
    j["spot_check_mod"] = spot_check_mod;
    j["version"] = version;
    j["width"] = width;

    j["loadscreen"] = loadscreen;

    j["no_rest"] = no_rest;
    j["pvp"] = pvp;
    j["shadow_opacity"] = shadow_opacity;
    j["skybox"] = skybox;

    return j;
}

#undef OBJECT_LIST_TO_JSON

} // namespace nw
