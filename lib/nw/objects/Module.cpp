#include "Module.hpp"

#include "../kernel/Kernel.hpp"
#include "Area.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool ModuleScripts::from_gff(const GffInputArchiveStruct& archive)
{
    archive.get_to("Mod_OnClientEntr", on_client_enter);
    archive.get_to("Mod_OnClientLeav", on_client_leave);
    archive.get_to("Mod_OnCutsnAbort", on_cutsnabort);
    archive.get_to("Mod_OnHeartbeat", on_heartbeat);
    archive.get_to("Mod_OnAcquirItem", on_item_acquire);
    archive.get_to("Mod_OnActvtItem", on_item_activate);
    archive.get_to("Mod_OnUnAqreItem", on_item_unaquire);
    archive.get_to("Mod_OnModLoad", on_load);
    archive.get_to("Mod_OnPlrChat", on_player_chat);
    archive.get_to("Mod_OnPlrDeath", on_player_death);
    archive.get_to("Mod_OnPlrDying", on_player_dying);
    archive.get_to("Mod_OnPlrEqItm", on_player_equip);
    archive.get_to("Mod_OnPlrLvlUp", on_player_level_up);
    archive.get_to("Mod_OnPlrRest", on_player_rest);
    archive.get_to("Mod_OnPlrUnEqItm", on_player_uneqiup);
    archive.get_to("Mod_OnSpawnBtnDn", on_spawnbtndn);
    archive.get_to("Mod_OnModStart", on_start);
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

bool ModuleScripts::to_gff(GffOutputArchiveStruct& archive) const
{
    archive.add_field("Mod_OnClientEntr", on_client_enter)
        .add_field("Mod_OnClientLeav", on_client_leave)
        .add_field("Mod_OnCutsnAbort", on_cutsnabort)
        .add_field("Mod_OnHeartbeat", on_heartbeat)
        .add_field("Mod_OnAcquirItem", on_item_acquire)
        .add_field("Mod_OnActvtItem", on_item_activate)
        .add_field("Mod_OnUnAqreItem", on_item_unaquire)
        .add_field("Mod_OnModLoad", on_load)
        .add_field("Mod_OnPlrChat", on_player_chat)
        .add_field("Mod_OnPlrDeath", on_player_death)
        .add_field("Mod_OnPlrDying", on_player_dying)
        .add_field("Mod_OnPlrEqItm", on_player_equip)
        .add_field("Mod_OnPlrLvlUp", on_player_level_up)
        .add_field("Mod_OnPlrRest", on_player_rest)
        .add_field("Mod_OnPlrUnEqItm", on_player_uneqiup)
        .add_field("Mod_OnSpawnBtnDn", on_spawnbtndn)
        .add_field("Mod_OnModStart", on_start)
        .add_field("Mod_OnUsrDefined", on_user_defined);

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

size_t Module::area_count() const noexcept
{
    if (std::holds_alternative<std::vector<flecs::entity>>(areas)) {
        return std::get<std::vector<flecs::entity>>(areas).size();
    }
    return 0;
}

flecs::entity Module::get_area(size_t index) const
{
    flecs::entity ent;
    if (std::holds_alternative<std::vector<flecs::entity>>(areas) && index < area_count()) {
        ent = std::get<std::vector<flecs::entity>>(areas)[index];
    }

    return ent;
}

bool Module::instantiate(flecs::entity ent)
{
    LOG_F(INFO, "instantiating module");
    auto mod = ent.get_mut<Module>();

    if (mod->haks.size()) {
        nw::kernel::resman().load_module_haks(mod->haks);
    }

    if (mod->tlk.size()) {
        auto path = nw::kernel::config().alias_path(PathAlias::tlk);
        nw::kernel::strings().load_custom_tlk(path / (mod->tlk + ".tlk"));
    }

    auto& area_list = std::get<std::vector<Resref>>(mod->areas);
    std::vector<flecs::entity> area_objects;
    area_objects.reserve(area_list.size());
    for (auto& area : area_list) {
        LOG_F(INFO, "  loading area: {}", area);
        area_objects.push_back(nw::kernel::objects().make_area(area));
    }
    mod->areas = std::move(area_objects);

    return true;
}

bool Module::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive)
{
    auto mod = ent.get_mut<Module>();
    auto scripts = ent.get_mut<ModuleScripts>();
    auto locals = ent.get_mut<LocalData>();

    locals->from_gff(archive);
    scripts->from_gff(archive);

    size_t sz = archive["Mod_Area_list"].size();
    auto st = archive["Mod_Area_list"];
    std::vector<Resref> area_list;
    area_list.reserve(sz);

    for (size_t i = 0; i < sz; ++i) {
        Resref r;
        if (st[i].get_to("Area_Name", r)) {
            area_list.push_back(r);
        } else {
            break;
        }
    }
    mod->areas = std::move(area_list);

    archive.get_to("Mod_Description", mod->description);
    archive.get_to("Mod_Entry_Area", mod->entry_area);
    archive.get_to("Mod_Entry_Dir_X", mod->entry_orientation.x);
    archive.get_to("Mod_Entry_Dir_Y", mod->entry_orientation.y);
    archive.get_to("Mod_Entry_X", mod->entry_position.x);
    archive.get_to("Mod_Entry_Y", mod->entry_position.y);
    archive.get_to("Mod_Entry_Z", mod->entry_position.z);

    sz = archive["Mod_HakList"].size();
    st = archive["Mod_HakList"];
    mod->haks.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        std::string r;
        if (st[i].get_to("Mod_Hak", r)) {
            mod->haks.push_back(r);
        } else {
            break;
        }
    }

    archive.get_to("Mod_ID", mod->id);
    archive.get_to("Mod_MinGameVer", mod->min_game_version);
    archive.get_to("Mod_Name", mod->name);
    archive.get_to("Mod_StartMovie", mod->start_movie);
    archive.get_to("Mod_Tag", mod->tag);
    archive.get_to("Mod_CustomTlk", mod->tlk);

    archive.get_to("Mod_Creator_ID", mod->creator);
    archive.get_to("Mod_StartYear", mod->start_year);
    archive.get_to("Mod_Version", mod->version);

    archive.get_to("Expansion_Pack", mod->expansion_pack);

    archive.get_to("Mod_DawnHour", mod->dawn_hour);
    archive.get_to("Mod_DuskHour", mod->dusk_hour);
    archive.get_to("Mod_IsSaveGame", mod->is_save_game);
    archive.get_to("Mod_MinPerHour", mod->minutes_per_hour);
    archive.get_to("Mod_StartDay", mod->start_day);
    archive.get_to("Mod_StartHour", mod->start_hour);
    archive.get_to("Mod_StartMonth", mod->start_month);
    archive.get_to("Mod_XPScale", mod->xpscale);

    return true;
}

bool Module::deserialize(flecs::entity ent, const nlohmann::json& archive)
{
    auto mod = ent.get_mut<Module>();
    auto scripts = ent.get_mut<ModuleScripts>();
    auto locals = ent.get_mut<LocalData>();

    try {
        locals->from_json(archive.at("locals"));
        scripts->from_json(archive.at("scripts"));

        std::vector<Resref> area_list;
        archive.at("areas").get_to(area_list);
        mod->areas = std::move(area_list);

        archive.at("description").get_to(mod->description);
        mod->entry_orientation.x = archive.at("entry_orientation")[0].get<float>();
        mod->entry_orientation.y = archive.at("entry_orientation")[1].get<float>();
        // entry_orientation.z = archive.at("entry_orientation")[2].get<float>();
        mod->entry_position.x = archive.at("entry_position")[0].get<float>();
        mod->entry_position.y = archive.at("entry_position")[1].get<float>();
        mod->entry_position.z = archive.at("entry_position")[2].get<float>();

        archive.at("haks").get_to(mod->haks);
        archive.at("id").get_to(mod->id);
        archive.at("min_game_version").get_to(mod->min_game_version);
        archive.at("name").get_to(mod->name);
        archive.at("start_movie").get_to(mod->start_movie);
        archive.at("tag").get_to(mod->tag);
        archive.at("tlk").get_to(mod->tlk);

        archive.at("creator").get_to(mod->creator);
        archive.at("start_year").get_to(mod->start_year);
        archive.at("version").get_to(mod->version);

        archive.at("expansion_pack").get_to(mod->expansion_pack);

        archive.at("dawn_hour").get_to(mod->dawn_hour);
        archive.at("dusk_hour").get_to(mod->dusk_hour);
        archive.at("entry_area").get_to(mod->entry_area);
        archive.at("is_save_game").get_to(mod->is_save_game);
        archive.at("minutes_per_hour").get_to(mod->minutes_per_hour);
        archive.at("start_day").get_to(mod->start_day);
        archive.at("start_hour").get_to(mod->start_hour);
        archive.at("start_month").get_to(mod->start_month);
        archive.at("xpscale").get_to(mod->xpscale);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Module::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Module::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive)
{
    auto mod = ent.get<Module>();
    auto scripts = ent.get<ModuleScripts>();
    auto locals = ent.get<LocalData>();

    locals->to_gff(archive);
    scripts->to_gff(archive);

    auto& area_list = archive.add_list("Mod_Area_list");
    if (std::holds_alternative<std::vector<Resref>>(mod->areas)) {
        for (const auto& area : std::get<std::vector<Resref>>(mod->areas)) {
            area_list.push_back(6).add_field("Area_Name", area);
        }
    } else {
        for (const auto& area : std::get<std::vector<flecs::entity>>(mod->areas)) {
            area_list.push_back(6).add_field("Area_Name", area.get<Common>()->resref);
        }
    }

    archive.add_field("Mod_Description", mod->description);

    archive.add_field("Mod_Entry_Area", mod->entry_area)
        .add_field("Mod_Entry_Dir_X", mod->entry_orientation.x)
        .add_field("Mod_Entry_Dir_Y", mod->entry_orientation.y)
        .add_field("Mod_Entry_X", mod->entry_position.x)
        .add_field("Mod_Entry_Y", mod->entry_position.y)
        .add_field("Mod_Entry_Z", mod->entry_position.z);

    auto& hak_list = archive.add_list("Mod_HakList");
    for (const auto& hak : mod->haks) {
        hak_list.push_back(8).add_field("Mod_Hak", hak);
    }

    archive.add_field("Mod_ID", mod->id);
    archive.add_field("Mod_MinGameVer", mod->min_game_version);
    archive.add_field("Mod_Name", mod->name);
    archive.add_field("Mod_StartMovie", mod->start_movie);
    archive.add_field("Mod_Tag", mod->tag);
    archive.add_field("Mod_CustomTlk", mod->tlk);

    // Always empty, obsolete, but NWN as of 1.69, at least, kept them in the GFF.
    archive.add_list("Mod_CutSceneList");
    archive.add_list("Mod_Expan_List");
    archive.add_list("Mod_GVar_List");

    archive.add_field("Mod_Creator_ID", mod->creator)
        .add_field("Mod_StartYear", mod->start_year)
        .add_field("Mod_Version", mod->version);

    archive.add_field("Expansion_Pack", mod->expansion_pack);

    archive.add_field("Mod_DawnHour", mod->dawn_hour)
        .add_field("Mod_DuskHour", mod->dusk_hour)
        .add_field("Mod_IsSaveGame", mod->is_save_game)
        .add_field("Mod_MinPerHour", mod->minutes_per_hour)
        .add_field("Mod_StartDay", mod->start_day)
        .add_field("Mod_StartHour", mod->start_hour)
        .add_field("Mod_StartMonth", mod->start_month)
        .add_field("Mod_XPScale", mod->xpscale);

    return true;
}

GffOutputArchive Module::serialize(const flecs::entity ent)
{
    GffOutputArchive out{"IFO"};
    Module::serialize(ent, out.top);
    out.build();
    return out;
}

bool Module::serialize(const flecs::entity ent, nlohmann::json& archive)
{
    auto mod = ent.get<Module>();
    auto scripts = ent.get<ModuleScripts>();
    auto locals = ent.get<LocalData>();

    archive["$type"] = "IFO";
    archive["$version"] = json_archive_version;

    archive["locals"] = locals->to_json(SerializationProfile::any);
    archive["scripts"] = scripts->to_json();

    if (std::holds_alternative<std::vector<Resref>>(mod->areas)) {
        archive["areas"] = std::get<std::vector<Resref>>(mod->areas);
    } else {
        auto& area_list = archive["areas"] = nlohmann::json::array();
        for (const auto area : std::get<std::vector<flecs::entity>>(mod->areas)) {
            area_list.push_back(area.get<Common>()->resref);
        }
    }

    archive["description"] = mod->description;
    archive["entry_area"] = mod->entry_area;

    archive["entry_orientation"] = nlohmann::json{
        mod->entry_orientation.x,
        mod->entry_orientation.y};

    archive["entry_position"] = nlohmann::json{
        mod->entry_position.x,
        mod->entry_position.y,
        mod->entry_position.z};

    archive["haks"] = mod->haks;
    archive["id"] = mod->id;
    archive["min_game_version"] = mod->min_game_version;
    archive["name"] = mod->name;
    archive["start_movie"] = mod->start_movie;
    archive["start_year"] = mod->start_year;
    archive["tag"] = mod->tag;
    archive["tlk"] = mod->tlk;

    archive["version"] = mod->version;
    archive["creator"] = mod->creator;

    archive["expansion_pack"] = mod->expansion_pack;

    archive["dawn_hour"] = mod->dawn_hour;
    archive["dusk_hour"] = mod->dusk_hour;
    archive["is_save_game"] = mod->is_save_game;
    archive["minutes_per_hour"] = mod->minutes_per_hour;
    archive["start_day"] = mod->start_day;
    archive["start_hour"] = mod->start_hour;
    archive["start_month"] = mod->start_month;
    archive["xpscale"] = mod->xpscale;

    return true;
}

} // namespace nw
