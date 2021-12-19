#include "Module.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool ModuleScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("Mod_OnAcquirItem", on_item_acquire);
    archive.get_to("Mod_OnActvtItem", on_item_activate);
    archive.get_to("Mod_OnClientEntr", on_client_enter);
    archive.get_to("Mod_OnClientLeav", on_client_leave);
    archive.get_to("Mod_OnCutsnAbort", on_cutsnabort);
    archive.get_to("Mod_OnHeartbeat", on_heartbeat);
    archive.get_to("Mod_OnModLoad", on_load);
    archive.get_to("Mod_OnModStart", on_start);
    archive.get_to("Mod_OnPlrChat", on_player_chat);
    archive.get_to("Mod_OnPlrDeath", on_player_death);
    archive.get_to("Mod_OnPlrDying", on_player_dying);
    archive.get_to("Mod_OnPlrEqItm", on_player_equip);
    archive.get_to("Mod_OnPlrLvlUp", on_player_level_up);
    archive.get_to("Mod_OnPlrRest", on_player_rest);
    archive.get_to("Mod_OnPlrUnEqItm", on_player_uneqiup);
    archive.get_to("Mod_OnSpawnBtnDn", on_spawnbtndn);
    archive.get_to("Mod_OnUnAqreItem", on_item_unaquire);
    archive.get_to("Mod_OnUsrDefined", on_user_defined);
    return true;
}

bool ModuleScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_client_enter").get_to(on_client_enter);
        archive.at("on_client_leave").get_to(on_client_leave);
        archive.at("on_cutsnabort").get_to(on_cutsnabort);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_item_acquire").get_to(on_item_acquire);
        archive.at("on_item_activate").get_to(on_item_activate);
        archive.at("on_item_unaquire").get_to(on_item_unaquire);
        archive.at("on_load").get_to(on_load);
        archive.at("on_player_chat").get_to(on_player_chat);
        archive.at("on_player_death").get_to(on_player_death);
        archive.at("on_player_dying").get_to(on_player_dying);
        archive.at("on_player_equip").get_to(on_player_equip);
        archive.at("on_player_level_up").get_to(on_player_level_up);
        archive.at("on_player_rest").get_to(on_player_rest);
        archive.at("on_player_uneqiup").get_to(on_player_uneqiup);
        archive.at("on_spawnbtndn").get_to(on_spawnbtndn);
        archive.at("on_start").get_to(on_start);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "ModuleScripts::from_json exception: {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json ModuleScripts::to_json() const
{
    nlohmann::json j;
    j["on_client_enter"] = on_client_enter;
    j["on_client_leave"] = on_client_leave;
    j["on_cutsnabort"] = on_cutsnabort;
    j["on_heartbeat"] = on_heartbeat;
    j["on_item_acquire"] = on_item_acquire;
    j["on_item_activate"] = on_item_activate;
    j["on_item_unaquire"] = on_item_unaquire;
    j["on_load"] = on_load;
    j["on_player_chat"] = on_player_chat;
    j["on_player_death"] = on_player_death;
    j["on_player_dying"] = on_player_dying;
    j["on_player_equip"] = on_player_equip;
    j["on_player_level_up"] = on_player_level_up;
    j["on_player_rest"] = on_player_rest;
    j["on_player_uneqiup"] = on_player_uneqiup;
    j["on_spawnbtndn"] = on_spawnbtndn;
    j["on_start"] = on_start;
    j["on_user_defined"] = on_user_defined;
    return j;
}

Module::Module(const GffInputArchiveStruct& archive)
{
    this->from_gff(archive);
}

Module::Module(const nlohmann::json& archive)
{
    this->from_json(archive);
}

bool Module::from_gff(const GffInputArchiveStruct& archive)
{
    local_data.from_gff(archive);
    archive.get_to("Expansion_Pack", expansion_pack);

    size_t sz = archive["Mod_Area_list"].size();
    auto st = archive["Mod_Area_list"];
    areas.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        Resref r;
        if (st[i].get_to("Area_Name", r)) {
            areas.push_back(r);
        } else {
            break;
        }
    }

    archive.get_to("Mod_Creator_ID", creator);
    archive.get_to("Mod_CustomTlk", tlk);
    archive.get_to("Mod_DawnHour", dawn_hour);
    archive.get_to("Mod_Description", description);
    archive.get_to("Mod_DuskHour", dusk_hour);
    archive.get_to("Mod_Entry_Area", entry_area);
    archive.get_to("Mod_Entry_Dir_X", entry_orientation.x);
    archive.get_to("Mod_Entry_Dir_Y", entry_orientation.y);
    archive.get_to("Mod_Entry_X", entry_position.x);
    archive.get_to("Mod_Entry_Y", entry_position.y);
    archive.get_to("Mod_Entry_Z", entry_position.z);

    sz = archive["Mod_HakList"].size();
    st = archive["Mod_HakList"];
    haks.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        Resref r;
        if (st[i].get_to("Mod_Hak", r)) {
            areas.push_back(r);
        } else {
            break;
        }
    }

    scripts.from_gff(archive);

    archive.get_to("Mod_ID", id);
    archive.get_to("Mod_IsSaveGame", is_save_game);
    archive.get_to("Mod_MinGameVer", min_game_version);
    archive.get_to("Mod_MinPerHour", minutes_per_hour);
    archive.get_to("Mod_Name", name);
    archive.get_to("Mod_StartDay", start_day);
    archive.get_to("Mod_StartHour", start_hour);
    archive.get_to("Mod_StartMonth", start_month);
    archive.get_to("Mod_StartMovie", start_movie);
    archive.get_to("Mod_StartYear", start_year);
    archive.get_to("Mod_Tag", tag);
    archive.get_to("Mod_Version", version);
    archive.get_to("Mod_XPScale", xpscale);

    return true;
}

bool Module::from_json(const nlohmann::json& archive)
{
    try {
        local_data.from_json(archive.at("local_data"));
        scripts.from_json(archive.at("scripts"));

        entry_orientation.x = archive.at("entry_orientation")[0].get<float>();
        entry_orientation.y = archive.at("entry_orientation")[1].get<float>();
        entry_orientation.z = archive.at("entry_orientation")[2].get<float>();
        entry_position.x = archive.at("entry_position")[0].get<float>();
        entry_position.y = archive.at("entry_position")[1].get<float>();
        entry_position.z = archive.at("entry_position")[2].get<float>();

        archive.at("areas").get_to(areas);
        archive.at("creator").get_to(creator);
        archive.at("dawn_hour").get_to(dawn_hour);
        archive.at("description").get_to(description);
        archive.at("dusk_hour").get_to(dusk_hour);
        archive.at("entry_area").get_to(entry_area);
        archive.at("expansion_pack").get_to(expansion_pack);
        archive.at("haks").get_to(haks);
        archive.at("id").get_to(id);
        archive.at("is_save_game").get_to(is_save_game);
        archive.at("min_game_version").get_to(min_game_version);
        archive.at("minutes_per_hour").get_to(minutes_per_hour);
        archive.at("name").get_to(name);
        archive.at("start_day").get_to(start_day);
        archive.at("start_hour").get_to(start_hour);
        archive.at("start_month").get_to(start_month);
        archive.at("start_movie").get_to(start_movie);
        archive.at("start_year").get_to(start_year);
        archive.at("tag").get_to(tag);
        archive.at("tlk").get_to(tlk);
        archive.at("version").get_to(version);
        archive.at("xpscale").get_to(xpscale);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Module::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

nlohmann::json Module::to_json() const
{
    nlohmann::json j;

    j["$type"] = "IFO";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["areas"] = areas;
    j["description"] = description;
    j["entry_area"] = entry_area;
    j["entry_orientation"] = nlohmann::json{entry_orientation.x, entry_orientation.y, entry_orientation.z};
    j["entry_position"] = nlohmann::json{entry_position.x, entry_position.y, entry_position.z};
    j["haks"] = haks;
    j["id"] = id;
    j["local_data"] = local_data.to_json(SerializationProfile::any);
    j["min_game_version"] = min_game_version;
    j["name"] = name;
    j["scripts"] = scripts.to_json();
    j["start_movie"] = start_movie;
    j["start_year"] = start_year;
    j["tag"] = tag;
    j["tlk"] = tlk;

    j["version"] = version;
    j["creator"] = creator;

    j["expansion_pack"] = expansion_pack;

    j["dawn_hour"] = dawn_hour;
    j["dusk_hour"] = dusk_hour;
    j["is_save_game"] = is_save_game;
    j["minutes_per_hour"] = minutes_per_hour;
    j["start_day"] = start_day;
    j["start_hour"] = start_hour;
    j["start_month"] = start_month;
    j["xpscale"] = xpscale;

    return j;
}

} // namespace nw
