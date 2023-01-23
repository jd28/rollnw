#include "Module.hpp"

#include "../kernel/Objects.hpp"
#include "../kernel/Strings.hpp"
#include "Area.hpp"

#include <nlohmann/json.hpp>

namespace nw {

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

size_t Module::area_count() const noexcept
{
    if (std::holds_alternative<std::vector<Area*>>(areas)) {
        return std::get<std::vector<Area*>>(areas).size();
    }
    return 0;
}

const Area* Module::get_area(size_t index) const
{
    if (std::holds_alternative<std::vector<Area*>>(areas) && index < area_count()) {
        return std::get<std::vector<Area*>>(areas)[index];
    }

    return nullptr;
}

bool Module::instantiate()
{
    if (instantiated_) return true;
    LOG_F(INFO, "instantiating module");

    if (haks.size()) {
        nw::kernel::resman().load_module_haks(haks);
    }

    if (tlk.size()) {
        auto path = nw::kernel::config().alias_path(PathAlias::tlk);
        nw::kernel::strings().load_custom_tlk(path / (tlk + ".tlk"));
    }

    auto& area_list = std::get<std::vector<Resref>>(areas);
    std::vector<Area*> area_objects;
    area_objects.reserve(area_list.size());
    for (auto& area : area_list) {
        LOG_F(INFO, "  loading area: {}", area);
        area_objects.push_back(nw::kernel::objects().make_area(area));
    }
    areas = std::move(area_objects);

    instantiated_ = true;
    return true;
}

bool Module::deserialize(Module* obj, const nlohmann::json& archive)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    try {
        obj->locals.from_json(archive.at("locals"));
        obj->scripts.from_json(archive.at("scripts"));

        std::vector<Resref> area_list;
        archive.at("areas").get_to(area_list);
        obj->areas = std::move(area_list);

        archive.at("description").get_to(obj->description);
        obj->entry_orientation.x = archive.at("entry_orientation")[0].get<float>();
        obj->entry_orientation.y = archive.at("entry_orientation")[1].get<float>();
        // entry_orientation.z = archive.at("entry_orientation")[2].get<float>();
        obj->entry_position.x = archive.at("entry_position")[0].get<float>();
        obj->entry_position.y = archive.at("entry_position")[1].get<float>();
        obj->entry_position.z = archive.at("entry_position")[2].get<float>();

        archive.at("haks").get_to(obj->haks);
        archive.at("id").get_to(obj->id);
        archive.at("min_game_version").get_to(obj->min_game_version);
        archive.at("name").get_to(obj->name);
        archive.at("start_movie").get_to(obj->start_movie);
        archive.at("tag").get_to(obj->tag);
        archive.at("tlk").get_to(obj->tlk);

        archive.at("creator").get_to(obj->creator);
        archive.at("start_year").get_to(obj->start_year);
        archive.at("version").get_to(obj->version);

        archive.at("expansion_pack").get_to(obj->expansion_pack);

        archive.at("dawn_hour").get_to(obj->dawn_hour);
        archive.at("dusk_hour").get_to(obj->dusk_hour);
        archive.at("entry_area").get_to(obj->entry_area);
        archive.at("is_save_game").get_to(obj->is_save_game);
        archive.at("minutes_per_hour").get_to(obj->minutes_per_hour);
        archive.at("start_day").get_to(obj->start_day);
        archive.at("start_hour").get_to(obj->start_hour);
        archive.at("start_month").get_to(obj->start_month);
        archive.at("xpscale").get_to(obj->xpscale);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Module::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

// == Module - Serialization - Gff ============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Module* obj, const GffStruct& archive)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    deserialize(obj->locals, archive);
    deserialize(obj->scripts, archive);

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
    obj->areas = std::move(area_list);

    archive.get_to("Mod_Description", obj->description);
    archive.get_to("Mod_Entry_Area", obj->entry_area);
    archive.get_to("Mod_Entry_Dir_X", obj->entry_orientation.x);
    archive.get_to("Mod_Entry_Dir_Y", obj->entry_orientation.y);
    archive.get_to("Mod_Entry_X", obj->entry_position.x);
    archive.get_to("Mod_Entry_Y", obj->entry_position.y);
    archive.get_to("Mod_Entry_Z", obj->entry_position.z);

    sz = archive["Mod_HakList"].size();
    st = archive["Mod_HakList"];
    obj->haks.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        std::string r;
        if (st[i].get_to("Mod_Hak", r)) {
            obj->haks.push_back(r);
        } else {
            break;
        }
    }

    archive.get_to("Mod_ID", obj->id);
    archive.get_to("Mod_MinGameVer", obj->min_game_version);
    archive.get_to("Mod_Name", obj->name);
    archive.get_to("Mod_StartMovie", obj->start_movie);
    archive.get_to("Mod_Tag", obj->tag);
    archive.get_to("Mod_CustomTlk", obj->tlk);

    archive.get_to("Mod_Creator_ID", obj->creator);
    archive.get_to("Mod_StartYear", obj->start_year);
    archive.get_to("Mod_Version", obj->version);

    archive.get_to("Expansion_Pack", obj->expansion_pack);

    archive.get_to("Mod_DawnHour", obj->dawn_hour);
    archive.get_to("Mod_DuskHour", obj->dusk_hour);
    archive.get_to("Mod_IsSaveGame", obj->is_save_game);
    archive.get_to("Mod_MinPerHour", obj->minutes_per_hour);
    archive.get_to("Mod_StartDay", obj->start_day);
    archive.get_to("Mod_StartHour", obj->start_hour);
    archive.get_to("Mod_StartMonth", obj->start_month);
    archive.get_to("Mod_XPScale", obj->xpscale);

    return true;
}

bool Module::serialize(const Module* obj, nlohmann::json& archive)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "IFO";
    archive["$version"] = json_archive_version;

    archive["locals"] = obj->locals.to_json(SerializationProfile::any);
    archive["scripts"] = obj->scripts.to_json();

    if (std::holds_alternative<std::vector<Resref>>(obj->areas)) {
        archive["areas"] = std::get<std::vector<Resref>>(obj->areas);
    } else {
        auto& area_list = archive["areas"] = nlohmann::json::array();
        for (const auto area : std::get<std::vector<Area*>>(obj->areas)) {
            area_list.push_back(area->common.resref);
        }
    }

    archive["description"] = obj->description;
    archive["entry_area"] = obj->entry_area;

    archive["entry_orientation"] = nlohmann::json{
        obj->entry_orientation.x,
        obj->entry_orientation.y};

    archive["entry_position"] = nlohmann::json{
        obj->entry_position.x,
        obj->entry_position.y,
        obj->entry_position.z};

    archive["haks"] = obj->haks;
    archive["id"] = obj->id;
    archive["min_game_version"] = obj->min_game_version;
    archive["name"] = obj->name;
    archive["start_movie"] = obj->start_movie;
    archive["start_year"] = obj->start_year;
    archive["tag"] = obj->tag;
    archive["tlk"] = obj->tlk;

    archive["version"] = obj->version;
    archive["creator"] = obj->creator;

    archive["expansion_pack"] = obj->expansion_pack;

    archive["dawn_hour"] = obj->dawn_hour;
    archive["dusk_hour"] = obj->dusk_hour;
    archive["is_save_game"] = obj->is_save_game;
    archive["minutes_per_hour"] = obj->minutes_per_hour;
    archive["start_day"] = obj->start_day;
    archive["start_hour"] = obj->start_hour;
    archive["start_month"] = obj->start_month;
    archive["xpscale"] = obj->xpscale;

    return true;
}

bool serialize(const Module* obj, GffBuilderStruct& archive)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj->locals, archive, SerializationProfile::any);
    serialize(obj->scripts, archive);

    auto& area_list = archive.add_list("Mod_Area_list");
    if (std::holds_alternative<std::vector<Resref>>(obj->areas)) {
        for (const auto& area : std::get<std::vector<Resref>>(obj->areas)) {
            area_list.push_back(6).add_field("Area_Name", area);
        }
    } else {
        for (const auto& area : std::get<std::vector<Area*>>(obj->areas)) {
            area_list.push_back(6).add_field("Area_Name", area->common.resref);
        }
    }

    archive.add_field("Mod_Description", obj->description);

    archive.add_field("Mod_Entry_Area", obj->entry_area)
        .add_field("Mod_Entry_Dir_X", obj->entry_orientation.x)
        .add_field("Mod_Entry_Dir_Y", obj->entry_orientation.y)
        .add_field("Mod_Entry_X", obj->entry_position.x)
        .add_field("Mod_Entry_Y", obj->entry_position.y)
        .add_field("Mod_Entry_Z", obj->entry_position.z);

    auto& hak_list = archive.add_list("Mod_HakList");
    for (const auto& hak : obj->haks) {
        hak_list.push_back(8).add_field("Mod_Hak", hak);
    }

    archive.add_field("Mod_ID", obj->id);
    archive.add_field("Mod_MinGameVer", obj->min_game_version);
    archive.add_field("Mod_Name", obj->name);
    archive.add_field("Mod_StartMovie", obj->start_movie);
    archive.add_field("Mod_Tag", obj->tag);
    archive.add_field("Mod_CustomTlk", obj->tlk);

    // Always empty, obsolete, but NWN as of 1.69, at least, kept them in the GFF.
    archive.add_list("Mod_CutSceneList");
    archive.add_list("Mod_Expan_List");
    archive.add_list("Mod_GVar_List");

    archive.add_field("Mod_Creator_ID", obj->creator)
        .add_field("Mod_StartYear", obj->start_year)
        .add_field("Mod_Version", obj->version);

    archive.add_field("Expansion_Pack", obj->expansion_pack);

    archive.add_field("Mod_DawnHour", obj->dawn_hour)
        .add_field("Mod_DuskHour", obj->dusk_hour)
        .add_field("Mod_IsSaveGame", obj->is_save_game)
        .add_field("Mod_MinPerHour", obj->minutes_per_hour)
        .add_field("Mod_StartDay", obj->start_day)
        .add_field("Mod_StartHour", obj->start_hour)
        .add_field("Mod_StartMonth", obj->start_month)
        .add_field("Mod_XPScale", obj->xpscale);

    return true;
}

GffBuilder serialize(const Module* obj)
{
    GffBuilder out{"IFO"};
    if (!obj) return out;

    serialize(obj, out.top);
    out.build();
    return out;
}

bool deserialize(ModuleScripts& self, const GffStruct& archive)
{
    archive.get_to("Mod_OnClientEntr", self.on_client_enter);
    archive.get_to("Mod_OnClientLeav", self.on_client_leave);
    archive.get_to("Mod_OnCutsnAbort", self.on_cutsnabort);
    archive.get_to("Mod_OnHeartbeat", self.on_heartbeat);
    archive.get_to("Mod_OnAcquirItem", self.on_item_acquire);
    archive.get_to("Mod_OnActvtItem", self.on_item_activate);
    archive.get_to("Mod_OnUnAqreItem", self.on_item_unaquire);
    archive.get_to("Mod_OnModLoad", self.on_load);
    archive.get_to("Mod_OnPlrChat", self.on_player_chat);
    archive.get_to("Mod_OnPlrDeath", self.on_player_death);
    archive.get_to("Mod_OnPlrDying", self.on_player_dying);
    archive.get_to("Mod_OnPlrEqItm", self.on_player_equip);
    archive.get_to("Mod_OnPlrLvlUp", self.on_player_level_up);
    archive.get_to("Mod_OnPlrRest", self.on_player_rest);
    archive.get_to("Mod_OnPlrUnEqItm", self.on_player_uneqiup);
    archive.get_to("Mod_OnSpawnBtnDn", self.on_spawnbtndn);
    archive.get_to("Mod_OnModStart", self.on_start);
    archive.get_to("Mod_OnUsrDefined", self.on_user_defined);
    return true;
}

bool serialize(const ModuleScripts& self, GffBuilderStruct& archive)
{
    archive.add_field("Mod_OnClientEntr", self.on_client_enter)
        .add_field("Mod_OnClientLeav", self.on_client_leave)
        .add_field("Mod_OnCutsnAbort", self.on_cutsnabort)
        .add_field("Mod_OnHeartbeat", self.on_heartbeat)
        .add_field("Mod_OnAcquirItem", self.on_item_acquire)
        .add_field("Mod_OnActvtItem", self.on_item_activate)
        .add_field("Mod_OnUnAqreItem", self.on_item_unaquire)
        .add_field("Mod_OnModLoad", self.on_load)
        .add_field("Mod_OnPlrChat", self.on_player_chat)
        .add_field("Mod_OnPlrDeath", self.on_player_death)
        .add_field("Mod_OnPlrDying", self.on_player_dying)
        .add_field("Mod_OnPlrEqItm", self.on_player_equip)
        .add_field("Mod_OnPlrLvlUp", self.on_player_level_up)
        .add_field("Mod_OnPlrRest", self.on_player_rest)
        .add_field("Mod_OnPlrUnEqItm", self.on_player_uneqiup)
        .add_field("Mod_OnSpawnBtnDn", self.on_spawnbtndn)
        .add_field("Mod_OnModStart", self.on_start)
        .add_field("Mod_OnUsrDefined", self.on_user_defined);

    return true;
}

#endif

} // namespace nw
