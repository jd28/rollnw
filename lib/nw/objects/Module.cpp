#include "Module.hpp"

namespace nw {

Module::Module(const GffStruct gff)
    : local_data{gff}
{

    gff.get_to("Expansion_Pack", expansion_pack);

    size_t sz = gff["Mod_Area_list"].size();
    auto st = gff["Mod_Area_list"];
    areas.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        Resref r;
        if (st[i].get_to("Area_Name", r)) {
            areas.push_back(r);
        } else {
            break;
        }
    }

    gff.get_to("Mod_Creator_ID", creator);
    gff.get_to("Mod_CustomTlk", tlk);
    gff.get_to("Mod_DawnHour", dawn_hour);
    gff.get_to("Mod_Description", description);
    gff.get_to("Mod_DuskHour", dusk_hour);
    gff.get_to("Mod_Entry_Area", entry_area);
    gff.get_to("Mod_Entry_Dir_X", entry_orientation.x);
    gff.get_to("Mod_Entry_Dir_Y", entry_orientation.y);
    gff.get_to("Mod_Entry_X", entry_position.x);
    gff.get_to("Mod_Entry_Y", entry_position.y);
    gff.get_to("Mod_Entry_Z", entry_position.z);

    sz = gff["Mod_HakList"].size();
    st = gff["Mod_HakList"];
    haks.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        Resref r;
        if (st[i].get_to("Mod_Hak", r)) {
            areas.push_back(r);
        } else {
            break;
        }
    }

    gff.get_to("Mod_ID", id);
    gff.get_to("Mod_IsSaveGame", is_save_game);
    gff.get_to("Mod_MinGameVer", min_game_version);
    gff.get_to("Mod_MinPerHour", minutes_per_hour);
    gff.get_to("Mod_Name", name);
    gff.get_to("Mod_OnAcquirItem", on_item_acquire);
    gff.get_to("Mod_OnActvtItem", on_item_activate);
    gff.get_to("Mod_OnClientEntr", on_client_enter);
    gff.get_to("Mod_OnClientLeav", on_client_leave);
    gff.get_to("Mod_OnCutsnAbort", on_cutsnabort);
    gff.get_to("Mod_OnHeartbeat", on_heartbeat);
    gff.get_to("Mod_OnModLoad", on_load);
    gff.get_to("Mod_OnModStart", on_start);
    gff.get_to("Mod_OnPlrChat", on_player_chat);
    gff.get_to("Mod_OnPlrDeath", on_player_death);
    gff.get_to("Mod_OnPlrDying", on_player_dying);
    gff.get_to("Mod_OnPlrEqItm", on_player_equip);
    gff.get_to("Mod_OnPlrLvlUp", on_player_level_up);
    gff.get_to("Mod_OnPlrRest", on_player_rest);
    gff.get_to("Mod_OnPlrUnEqItm", on_player_uneqiup);
    gff.get_to("Mod_OnSpawnBtnDn", on_spawnbtndn);
    gff.get_to("Mod_OnUnAqreItem", on_item_unaquire);
    gff.get_to("Mod_OnUsrDefined", on_user_defined);
    gff.get_to("Mod_StartDay", start_day);
    gff.get_to("Mod_StartHour", start_hour);
    gff.get_to("Mod_StartMonth", start_month);
    gff.get_to("Mod_StartMovie", start_movie);
    gff.get_to("Mod_StartYear", start_year);
    gff.get_to("Mod_Tag", tag);
    gff.get_to("Mod_Version", version);
    gff.get_to("Mod_XPScale", xpscale);
}

} // namespace nw
