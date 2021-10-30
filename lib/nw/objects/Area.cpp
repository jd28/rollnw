#include "Area.hpp"

namespace nw {

AreaWeather::AreaWeather(const GffStruct gff)
{
    gff.get_to("ChanceLightning", chance_lightning);
    gff.get_to("ChanceRain", chance_rain);
    gff.get_to("ChanceSnow", chance_snow);
    gff.get_to("MoonAmbientColor", color_moon_ambient);
    gff.get_to("MoonDiffuseColor", color_moon_diffuse);
    gff.get_to("MoonFogColor", color_moon_fog);
    gff.get_to("SunAmbientColor", color_sun_ambient);
    gff.get_to("SunDiffuseColor", color_sun_diffuse);
    gff.get_to("SunFogColor", color_sun_fog);
    gff.get_to("FogClipDist", fog_clip_distance);
    gff.get_to("WindPower", wind_power);

    gff.get_to("DayNightCycle", day_night_cycle);
    gff.get_to("IsNight", is_night);
    gff.get_to("LightingScheme", lighting_scheme);
    gff.get_to("MoonFogAmount", fog_moon_amount);
    gff.get_to("MoonShadows", moon_shadows);
    gff.get_to("SunFogAmount", fog_sun_amount);
    gff.get_to("SunShadows", sun_shadows);
}

Tile::Tile(const GffStruct gff)
{
    gff.get_to("Tile_ID", id);
    gff.get_to("Tile_Height", height);
    gff.get_to("Tile_Orientation", orientation);
    gff.get_to("Tile_AnimLoop1", animloop1);
    gff.get_to("Tile_AnimLoop2", animloop2);
    gff.get_to("Tile_AnimLoop3", animloop3);
    gff.get_to("Tile_MainLight1", mainlight1);
    gff.get_to("Tile_MainLight2", mainlight2);
    gff.get_to("Tile_SrcLight1", srclight1);
    gff.get_to("Tile_SrcLight2", srclight);
}

Area::Area(const GffStruct caf, const GffStruct gic)
    : Area(caf, caf, gic)
{
    is_caf_ = true;
}

Area::Area(const GffStruct are, const GffStruct git, const GffStruct gic)
    : Object{ObjectType::area, are, SerializationProfile::blueprint}
    , weather{are}
{
    are.get_to("Flags", flags);
    are.get_to("Tileset", tileset);
    are.get_to("Height", height);
    are.get_to("ID", id);
    are.get_to("LoadScreenID", loadscreen);
    are.get_to("ModListenCheck", listen_check_mod);
    are.get_to("ModSpotCheck", spot_check_mod);
    are.get_to("Name", name);
    are.get_to("NoRest", no_rest);
    are.get_to("OnEnter", on_enter);
    are.get_to("OnExit", on_exit);
    are.get_to("OnHeartbeat", on_heartbeat);
    are.get_to("OnUserDefined", on_user_defined);
    are.get_to("PlayerVsPlayer", pvp);
    are.get_to("ShadowOpacity", shadow_opacity);
    are.get_to("SkyBox", skybox);
    are.get_to("Version", version);
    are.get_to("Width", width);

    auto st = are["Tile_List"];
    size_t sz = st.size();
    tiles.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        tiles.emplace_back(st[i]);
    }

#define GIT_LIST(name, holder, type)                                                            \
    do {                                                                                        \
        LOG_F(INFO, name);                                                                      \
        st = git[name];                                                                         \
        sz = st.size();                                                                         \
        holder.reserve(sz);                                                                     \
        for (size_t i = 0; i < sz; ++i) {                                                       \
            holder.emplace_back(std::make_unique<type>(st[i], SerializationProfile::instance)); \
        }                                                                                       \
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
}

} // namespace nw
