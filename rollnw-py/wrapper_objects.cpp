#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectBase.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Player.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/util/platform.hpp>
#include <nw/util/string.hpp>
#include <nw/util/templates.hpp>

#include <fmt/format.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <pybind11_json/pybind11_json.hpp>

namespace py = pybind11;
namespace fs = std::filesystem;

template <typename T>
nlohmann::json to_json_helper(const T* self, nw::SerializationProfile profile)
{
    nlohmann::json result;
    T::serialize(self, result, profile);
    return result;
}

template <typename T>
T* create_object_from_json_helper(const nlohmann::json& j)
{
    auto obj = new T;
    if constexpr (!std::is_same_v<T, nw::Player>) {
        T::deserialize(obj, j, nw::SerializationProfile::blueprint);
    } else {
        T::deserialize(obj, j);
    }
    return obj;
}

template <typename T>
T* create_object_from_file_helper(const std::filesystem::path& path)
{
    auto p = nw::expand_path(path);
    if (!fs::exists(path)) {
        throw std::runtime_error(fmt::format("{} does not exist", path));
    }
    auto ext = nw::path_to_string(p.extension());
    if (nw::string::icmp(ext, ".json")) {
        std::ifstream f(p);
        nlohmann::json data = nlohmann::json::parse(f);
        auto obj = new T;
        if constexpr (!std::is_same_v<T, nw::Player>) {
            T::deserialize(obj, data, nw::SerializationProfile::blueprint);
        } else {
            T::deserialize(obj, data);
        }
        return obj;
    } else if (nw::ResourceType::from_extension(ext) == T::restype) {
        nw::Gff data{p};
        auto obj = new T;
        if constexpr (!std::is_same_v<T, nw::Player>) {
            deserialize(obj, data.toplevel(), nw::SerializationProfile::blueprint);
        } else {
            deserialize(obj, data.toplevel());
        }
        return obj;
    }
    throw std::runtime_error(fmt::format("unknown file extension: {}", ext));
}

void init_object_components(py::module& nw);

void init_objects_base(py::module& nw)
{
    py::enum_<nw::ObjectID>(nw, "ObjectID");
    nw.attr("OBJECT_INVALID") = nw::object_invalid;

    py::enum_<nw::ObjectType>(nw, "ObjectType")
        .value("invalid", nw::ObjectType::invalid)
        .value("gui", nw::ObjectType::gui)
        .value("tile", nw::ObjectType::tile)
        .value("module", nw::ObjectType::module)
        .value("area", nw::ObjectType::area)
        .value("creature", nw::ObjectType::creature)
        .value("item", nw::ObjectType::item)
        .value("trigger", nw::ObjectType::trigger)
        .value("projectile", nw::ObjectType::projectile)
        .value("placeable", nw::ObjectType::placeable)
        .value("door", nw::ObjectType::door)
        .value("areaofeffect", nw::ObjectType::areaofeffect)
        .value("waypoint", nw::ObjectType::waypoint)
        .value("encounter", nw::ObjectType::encounter)
        .value("store", nw::ObjectType::store)
        .value("portal", nw::ObjectType::portal)
        .value("sound", nw::ObjectType::sound);

    py::class_<nw::ObjectHandle>(nw, "ObjectHandle")
        .def(py::init<>())
        .def("__repr__", [](nw::ObjectHandle self) {
            return fmt::format("<ObjectHandle id: {}, version: {}, type: {}",
                nw::to_underlying(self.id), self.version, nw::to_underlying(self.type));
        })
        .def_readonly("id", &nw::ObjectHandle::id)
        .def_readonly("version", &nw::ObjectHandle::version)
        .def_readonly("type", &nw::ObjectHandle::type)
        .def("valid", [](const nw::ObjectHandle& self) { return self.id != nw::object_invalid; });

    py::class_<nw::ObjectBase>(nw, "ObjectBase")
        .def("handle", &nw::ObjectBase::handle)
        .def("instantiate", &nw::ObjectBase::instantiate)
        //.def("valid", &nw::ObjectBase::valid)
        .def("as_common", py::overload_cast<>(&nw::ObjectBase::as_common), py::return_value_policy::reference_internal)
        .def("as_common", py::overload_cast<>(&nw::ObjectBase::as_common, py::const_), py::return_value_policy::reference_internal)
        .def("as_creature", py::overload_cast<>(&nw::ObjectBase::as_creature), py::return_value_policy::reference_internal)
        .def("as_creature", py::overload_cast<>(&nw::ObjectBase::as_creature, py::const_), py::return_value_policy::reference_internal)
        .def("as_door", py::overload_cast<>(&nw::ObjectBase::as_door), py::return_value_policy::reference_internal)
        .def("as_door", py::overload_cast<>(&nw::ObjectBase::as_door, py::const_), py::return_value_policy::reference_internal)
        .def("as_encounter", py::overload_cast<>(&nw::ObjectBase::as_encounter), py::return_value_policy::reference_internal)
        .def("as_encounter", py::overload_cast<>(&nw::ObjectBase::as_encounter, py::const_), py::return_value_policy::reference_internal)
        .def("as_item", py::overload_cast<>(&nw::ObjectBase::as_item), py::return_value_policy::reference_internal)
        .def("as_item", py::overload_cast<>(&nw::ObjectBase::as_item, py::const_), py::return_value_policy::reference_internal)
        .def("as_module", py::overload_cast<>(&nw::ObjectBase::as_module), py::return_value_policy::reference_internal)
        .def("as_module", py::overload_cast<>(&nw::ObjectBase::as_module, py::const_), py::return_value_policy::reference_internal)
        .def("as_placeable", py::overload_cast<>(&nw::ObjectBase::as_placeable), py::return_value_policy::reference_internal)
        .def("as_placeable", py::overload_cast<>(&nw::ObjectBase::as_placeable, py::const_), py::return_value_policy::reference_internal)
        .def("as_sound", py::overload_cast<>(&nw::ObjectBase::as_sound), py::return_value_policy::reference_internal)
        .def("as_sound", py::overload_cast<>(&nw::ObjectBase::as_sound, py::const_), py::return_value_policy::reference_internal)
        .def("as_trigger", py::overload_cast<>(&nw::ObjectBase::as_trigger), py::return_value_policy::reference_internal)
        .def("as_trigger", py::overload_cast<>(&nw::ObjectBase::as_trigger, py::const_), py::return_value_policy::reference_internal)
        .def("as_waypoint", py::overload_cast<>(&nw::ObjectBase::as_waypoint), py::return_value_policy::reference_internal)
        .def("as_waypoint", py::overload_cast<>(&nw::ObjectBase::as_waypoint, py::const_), py::return_value_policy::reference_internal);
}

void init_objects_area(py::module& nw)
{
    py::enum_<nw::AreaFlags>(nw, "AreaFlags")
        .value("none", nw::AreaFlags::none)
        .value("interior", nw::AreaFlags::interior)
        .value("underground", nw::AreaFlags::underground)
        .value("natural", nw::AreaFlags::natural);

    py::class_<nw::AreaScripts>(nw, "AreaScripts")
        .def(py::init<>())
        .def_readwrite("on_enter", &nw::AreaScripts::on_enter)
        .def_readwrite("on_exit", &nw::AreaScripts::on_exit)
        .def_readwrite("on_heartbeat", &nw::AreaScripts::on_heartbeat)
        .def_readwrite("on_user_defined", &nw::AreaScripts::on_user_defined);

    py::class_<nw::AreaWeather>(nw, "AreaWeather")
        .def(py::init<>())
        .def_readwrite("chance_lightning", &nw::AreaWeather::chance_lightning)
        .def_readwrite("chance_rain", &nw::AreaWeather::chance_rain)
        .def_readwrite("chance_snow", &nw::AreaWeather::chance_snow)
        .def_readwrite("color_moon_ambient", &nw::AreaWeather::color_moon_ambient)
        .def_readwrite("color_moon_diffuse", &nw::AreaWeather::color_moon_diffuse)
        .def_readwrite("color_moon_fog", &nw::AreaWeather::color_moon_fog)
        .def_readwrite("color_sun_ambient", &nw::AreaWeather::color_sun_ambient)
        .def_readwrite("color_sun_diffuse", &nw::AreaWeather::color_sun_diffuse)
        .def_readwrite("color_sun_fog", &nw::AreaWeather::color_sun_fog)
        .def_readwrite("fog_clip_distance", &nw::AreaWeather::fog_clip_distance)
        .def_readwrite("wind_power", &nw::AreaWeather::wind_power)
        .def_readwrite("day_night_cycle", &nw::AreaWeather::day_night_cycle)
        .def_readwrite("is_night", &nw::AreaWeather::is_night)
        .def_readwrite("lighting_scheme", &nw::AreaWeather::lighting_scheme)
        .def_readwrite("fog_moon_amount", &nw::AreaWeather::fog_moon_amount)
        .def_readwrite("moon_shadows", &nw::AreaWeather::moon_shadows)
        .def_readwrite("fog_sun_amount", &nw::AreaWeather::fog_sun_amount)
        .def_readwrite("sun_shadows", &nw::AreaWeather::sun_shadows);

    py::class_<nw::Tile>(nw, "Tile")
        .def(py::init<>())
        .def_readwrite("id", &nw::Tile::id)
        .def_readwrite("height", &nw::Tile::height)
        .def_readwrite("orientation", &nw::Tile::orientation)
        .def_readwrite("animloop1", &nw::Tile::animloop1)
        .def_readwrite("animloop2", &nw::Tile::animloop2)
        .def_readwrite("animloop3", &nw::Tile::animloop3)
        .def_readwrite("mainlight1", &nw::Tile::mainlight1)
        .def_readwrite("mainlight2", &nw::Tile::mainlight2)
        .def_readwrite("srclight1", &nw::Tile::srclight1)
        .def_readwrite("srclight2", &nw::Tile::srclight2);

    py::class_<nw::Area, nw::ObjectBase>(nw, "Area")
        .def(py::init<>())

        .def_readonly_static("json_archive_version", &nw::Area::json_archive_version)
        .def_readonly_static("object_type", &nw::Area::object_type)

        .def_readonly("creatures", &nw::Area::creatures)
        .def_readonly("doors", &nw::Area::doors)
        .def_readonly("encounters", &nw::Area::encounters)
        .def_readonly("items", &nw::Area::items)
        .def_readonly("placeables", &nw::Area::placeables)
        .def_readonly("sounds", &nw::Area::sounds)
        .def_readonly("stores", &nw::Area::stores)
        .def_readonly("triggers", &nw::Area::triggers)
        .def_readonly("waypoints", &nw::Area::waypoints)
        .def_readwrite("comments", &nw::Area::comments)
        .def_readwrite("name", &nw::Area::name)
        .def_readonly("scripts", &nw::Area::scripts)
        .def_readwrite("tileset", &nw::Area::tileset)
        .def_readonly("tiles", &nw::Area::tiles)
        .def_readwrite("weather", &nw::Area::weather)
        .def_readwrite("creator_id", &nw::Area::creator_id)
        .def_readwrite("flags", &nw::Area::flags)
        .def_readwrite("height", &nw::Area::height)
        .def_readwrite("id", &nw::Area::id)
        .def_readwrite("listen_check_mod", &nw::Area::listen_check_mod)
        .def_readwrite("spot_check_mod", &nw::Area::spot_check_mod)
        .def_readwrite("version", &nw::Area::version)
        .def_readwrite("width", &nw::Area::width)
        .def_readwrite("loadscreen", &nw::Area::loadscreen)
        .def_readwrite("no_rest", &nw::Area::no_rest)
        .def_readwrite("pvp", &nw::Area::pvp)
        .def_readwrite("shadow_opacity", &nw::Area::shadow_opacity)
        .def_readwrite("skybox", &nw::Area::skybox);
}

void init_objects_creature(py::module& nw)
{
    py::class_<nw::CreatureScripts>(nw, "CreatureScripts")
        .def_readwrite("on_attacked", &nw::CreatureScripts::on_attacked)
        .def_readwrite("on_blocked", &nw::CreatureScripts::on_blocked)
        .def_readwrite("on_conversation", &nw::CreatureScripts::on_conversation)
        .def_readwrite("on_damaged", &nw::CreatureScripts::on_damaged)
        .def_readwrite("on_death", &nw::CreatureScripts::on_death)
        .def_readwrite("on_disturbed", &nw::CreatureScripts::on_disturbed)
        .def_readwrite("on_endround", &nw::CreatureScripts::on_endround)
        .def_readwrite("on_heartbeat", &nw::CreatureScripts::on_heartbeat)
        .def_readwrite("on_perceived", &nw::CreatureScripts::on_perceived)
        .def_readwrite("on_rested", &nw::CreatureScripts::on_rested)
        .def_readwrite("on_spawn", &nw::CreatureScripts::on_spawn)
        .def_readwrite("on_spell_cast_at", &nw::CreatureScripts::on_spell_cast_at)
        .def_readwrite("on_user_defined", &nw::CreatureScripts::on_user_defined);

    py::class_<nw::Creature, nw::ObjectBase>(nw, "Creature")
        .def(py::init<>())

        .def_static("from_dict", &create_object_from_json_helper<nw::Creature>)
        .def_static("from_file", &create_object_from_file_helper<nw::Creature>)

        .def("to_dict", &to_json_helper<nw::Creature>)
        .def("handle", &nw::Creature::handle)

        .def_readonly_static("json_archive_version", &nw::Creature::json_archive_version)
        .def_readonly_static("object_type", &nw::Creature::object_type)

        .def_readwrite("common", &nw::Creature::common)
        .def_readwrite("appearance", &nw::Creature::appearance)
        //.def_readwrite("combat_info", &nw::Creature::combat_info)
        .def_readwrite("conversation", &nw::Creature::conversation)
        .def_readwrite("deity", &nw::Creature::deity)
        .def_readwrite("description", &nw::Creature::description)
        .def_property_readonly(
            "equipment", [](const nw::Creature& self) { return &self.equipment; }, py::return_value_policy::reference_internal)
        .def_property_readonly(
            "history", [](const nw::Creature& self) { return &self.history; }, py::return_value_policy::reference_internal)
        .def_property_readonly(
            "inventory", [](const nw::Creature& c) { return &c.inventory; }, py::return_value_policy::reference_internal)
        .def_readwrite("levels", &nw::Creature::levels)
        .def_readwrite("name_first", &nw::Creature::name_first)
        .def_readwrite("name_last", &nw::Creature::name_last)
        .def_readwrite("scripts", &nw::Creature::scripts)
        .def_readonly("stats", &nw::Creature::stats)
        .def_readwrite("subrace", &nw::Creature::subrace)

        .def_readwrite("cr", &nw::Creature::cr)
        .def_readwrite("cr_adjust", &nw::Creature::cr_adjust)
        .def_readwrite("decay_time", &nw::Creature::decay_time)
        .def_readwrite("walkrate", &nw::Creature::walkrate)

        .def_readwrite("faction_id", &nw::Creature::faction_id)
        .def_readwrite("hp", &nw::Creature::hp)
        .def_readwrite("hp_current", &nw::Creature::hp_current)
        .def_readwrite("hp_max", &nw::Creature::hp_max)
        .def_readwrite("soundset", &nw::Creature::soundset)

        .def_readwrite("bodybag", &nw::Creature::bodybag)
        .def_readwrite("chunk_death", &nw::Creature::chunk_death)
        .def_readwrite("disarmable", &nw::Creature::disarmable)
        .def_readwrite("gender", &nw::Creature::gender)
        .def_readwrite("good_evil", &nw::Creature::good_evil)
        .def_readwrite("immortal", &nw::Creature::immortal)
        .def_readwrite("interruptable", &nw::Creature::interruptable)
        .def_readwrite("lawful_chaotic", &nw::Creature::lawful_chaotic)
        .def_readwrite("lootable", &nw::Creature::lootable)
        .def_readwrite("pc", &nw::Creature::pc)
        .def_readwrite("perception_range", &nw::Creature::perception_range)
        .def_readwrite("plot", &nw::Creature::plot)
        .def_readwrite("race", &nw::Creature::race)
        .def_readwrite("starting_package", &nw::Creature::starting_package);
}

void init_objects_door(py::module& nw)
{
    py::enum_<nw::DoorAnimationState>(nw, "DoorAnimationState")
        .value("closed", nw::DoorAnimationState::closed)
        .value("opened1", nw::DoorAnimationState::opened1)
        .value("opened2", nw::DoorAnimationState::opened2);

    py::class_<nw::DoorScripts>(nw, "DoorScripts")
        .def_readwrite("on_click", &nw::DoorScripts::on_click)
        .def_readwrite("on_closed", &nw::DoorScripts::on_closed)
        .def_readwrite("on_damaged", &nw::DoorScripts::on_damaged)
        .def_readwrite("on_death", &nw::DoorScripts::on_death)
        .def_readwrite("on_disarm", &nw::DoorScripts::on_disarm)
        .def_readwrite("on_heartbeat", &nw::DoorScripts::on_heartbeat)
        .def_readwrite("on_lock", &nw::DoorScripts::on_lock)
        .def_readwrite("on_melee_attacked", &nw::DoorScripts::on_melee_attacked)
        .def_readwrite("on_open_failure", &nw::DoorScripts::on_open_failure)
        .def_readwrite("on_open", &nw::DoorScripts::on_open)
        .def_readwrite("on_spell_cast_at", &nw::DoorScripts::on_spell_cast_at)
        .def_readwrite("on_trap_triggered", &nw::DoorScripts::on_trap_triggered)
        .def_readwrite("on_unlock", &nw::DoorScripts::on_unlock)
        .def_readwrite("on_user_defined", &nw::DoorScripts::on_user_defined);

    py::class_<nw::Door, nw::ObjectBase>(nw, "Door")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Door>)

        .def_readonly_static("json_archive_version", &nw::Door::json_archive_version)
        .def_readonly_static("object_type", &nw::Door::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Door>)
        .def_static("from_file", &create_object_from_file_helper<nw::Door>)

        .def_readwrite("common", &nw::Door::common)
        .def_readwrite("conversation", &nw::Door::conversation)
        .def_readwrite("description", &nw::Door::description)
        .def_readwrite("linked_to", &nw::Door::linked_to)
        .def_readwrite("lock", &nw::Door::lock)
        .def_readwrite("saves", &nw::Door::saves)
        .def_readwrite("scripts", &nw::Door::scripts)
        .def_readwrite("trap", &nw::Door::trap)

        .def_readwrite("appearance", &nw::Door::appearance)
        .def_readwrite("faction", &nw::Door::faction)
        .def_readwrite("generic_type", &nw::Door::generic_type)

        .def_readwrite("hp", &nw::Door::hp)
        .def_readwrite("hp_current", &nw::Door::hp_current)
        .def_readwrite("loadscreen", &nw::Door::loadscreen)
        .def_readwrite("portrait_id", &nw::Door::portrait_id)

        .def_readwrite("animation_state", &nw::Door::animation_state)
        .def_readwrite("hardness", &nw::Door::hardness)
        .def_readwrite("interruptable", &nw::Door::interruptable)
        .def_readwrite("linked_to_flags", &nw::Door::linked_to_flags)
        .def_readwrite("plot", &nw::Door::plot);
}

void init_objects_encounter(py::module& nw)
{
    py::class_<nw::EncounterScripts>(nw, "EncounterScripts")
        .def_readwrite("on_entered", &nw::EncounterScripts::on_entered)
        .def_readwrite("on_exhausted", &nw::EncounterScripts::on_exhausted)
        .def_readwrite("on_exit", &nw::EncounterScripts::on_exit)
        .def_readwrite("on_heartbeat", &nw::EncounterScripts::on_heartbeat)
        .def_readwrite("on_user_defined", &nw::EncounterScripts::on_user_defined);

    py::class_<nw::SpawnCreature>(nw, "SpawnCreature")
        .def_readwrite("appearance", &nw::SpawnCreature::appearance)
        .def_readwrite("cr", &nw::SpawnCreature::cr)
        .def_readwrite("resref", &nw::SpawnCreature::resref)
        .def_readwrite("single_spawn", &nw::SpawnCreature::single_spawn);

    py::class_<nw::SpawnPoint>(nw, "SpawnPoint")
        .def_readwrite("orientation", &nw::SpawnPoint::orientation)
        .def_readwrite("position", &nw::SpawnPoint::position);

    py::class_<nw::Encounter, nw::ObjectBase>(nw, "Encounter")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Encounter>)

        .def_readonly_static("json_archive_version", &nw::Encounter::json_archive_version)
        .def_readonly_static("object_type", &nw::Encounter::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Encounter>)
        .def_static("from_file", &create_object_from_file_helper<nw::Encounter>)

        .def_readwrite("common", &nw::Encounter::common)
        .def_readwrite("creatures", &nw::Encounter::creatures)
        .def_readwrite("geometry", &nw::Encounter::geometry)
        .def_readonly("scripts", &nw::Encounter::scripts)
        .def_readwrite("spawn_points", &nw::Encounter::spawn_points)

        .def_readwrite("creatures_max", &nw::Encounter::creatures_max)
        .def_readwrite("creatures_recommended", &nw::Encounter::creatures_recommended)
        .def_readwrite("difficulty", &nw::Encounter::difficulty)
        .def_readwrite("difficulty_index", &nw::Encounter::difficulty_index)
        .def_readwrite("faction", &nw::Encounter::faction)
        .def_readwrite("reset_time", &nw::Encounter::reset_time)
        .def_readwrite("respawns", &nw::Encounter::respawns)
        .def_readwrite("spawn_option", &nw::Encounter::spawn_option)

        .def_readwrite("active", &nw::Encounter::active)
        .def_readwrite("player_only", &nw::Encounter::player_only)
        .def_readwrite("reset", &nw::Encounter::reset);
}

void init_objects_item(py::module& nw)
{
    py::enum_<nw::ItemModelType>(nw, "ItemModelType")
        .value("simple", nw::ItemModelType::simple)
        .value("layered", nw::ItemModelType::layered)
        .value("composite", nw::ItemModelType::composite)
        .value("armor", nw::ItemModelType::armor);

    py::enum_<nw::ItemColors::type>(nw, "ItemColors")
        .value("cloth1", nw::ItemColors::cloth1)
        .value("cloth2", nw::ItemColors::cloth2)
        .value("leather1", nw::ItemColors::leather1)
        .value("leather2", nw::ItemColors::leather2)
        .value("metal1", nw::ItemColors::metal1)
        .value("metal2", nw::ItemColors::metal2);

    py::enum_<nw::ItemModelParts::type>(nw, "ItemModelParts")
        .value("model1", nw::ItemModelParts::model1)
        .value("model2", nw::ItemModelParts::model2)
        .value("model3", nw::ItemModelParts::model3)
        .value("armor_belt", nw::ItemModelParts::armor_belt)
        .value("armor_lbicep", nw::ItemModelParts::armor_lbicep)
        .value("armor_lfarm", nw::ItemModelParts::armor_lfarm)
        .value("armor_lfoot", nw::ItemModelParts::armor_lfoot)
        .value("armor_lhand", nw::ItemModelParts::armor_lhand)
        .value("armor_lshin", nw::ItemModelParts::armor_lshin)
        .value("armor_lshoul", nw::ItemModelParts::armor_lshoul)
        .value("armor_lthigh", nw::ItemModelParts::armor_lthigh)
        .value("armor_neck", nw::ItemModelParts::armor_neck)
        .value("armor_pelvis", nw::ItemModelParts::armor_pelvis)
        .value("armor_rbicep", nw::ItemModelParts::armor_rbicep)
        .value("armor_rfarm", nw::ItemModelParts::armor_rfarm)
        .value("armor_rfoot", nw::ItemModelParts::armor_rfoot)
        .value("armor_rhand", nw::ItemModelParts::armor_rhand)
        .value("armor_robe", nw::ItemModelParts::armor_robe)
        .value("armor_rshin", nw::ItemModelParts::armor_rshin)
        .value("armor_rshoul", nw::ItemModelParts::armor_rshoul)
        .value("armor_rthigh", nw::ItemModelParts::armor_rthigh)
        .value("armor_torso", nw::ItemModelParts::armor_torso);

    py::class_<nw::ItemProperty>(nw, "ItemProperty")
        .def_readwrite("type", &nw::ItemProperty::type)
        .def_readwrite("subtype", &nw::ItemProperty::subtype)
        .def_readwrite("cost_table", &nw::ItemProperty::cost_table)
        .def_readwrite("cost_value", &nw::ItemProperty::cost_value)
        .def_readwrite("param_table", &nw::ItemProperty::param_table)
        .def_readwrite("param_value", &nw::ItemProperty::param_value);

    py::class_<nw::Item, nw::ObjectBase>(nw, "Item")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Item>)
        .def("handle", &nw::Item::handle)

        .def_readonly_static("json_archive_version", &nw::Item::json_archive_version)
        .def_readonly_static("object_type", &nw::Item::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Item>)
        .def_static("from_file", &create_object_from_file_helper<nw::Item>)

        .def_readwrite("common", &nw::Item::common)

        .def_readwrite("description", &nw::Item::description)
        .def_readwrite("description_id", &nw::Item::description_id)
        .def_property_readonly(
            "inventory", [](const nw::Item& i) { return &i.inventory; }, py::return_value_policy::reference_internal)
        .def_readwrite("properties", &nw::Item::properties)

        .def_readwrite("cost", &nw::Item::cost)
        .def_readwrite("additional_cost", &nw::Item::additional_cost)
        .def_readwrite("baseitem", &nw::Item::baseitem)

        .def_readwrite("stacksize", &nw::Item::stacksize)

        .def_readwrite("charges", &nw::Item::charges)
        .def_readwrite("cursed", &nw::Item::cursed)
        .def_readwrite("identified", &nw::Item::identified)
        .def_readwrite("plot", &nw::Item::plot)
        .def_readwrite("stolen", &nw::Item::stolen)

        .def_readwrite("model_type", &nw::Item::model_type)
        .def_readwrite("model_colors", &nw::Item::model_colors)
        .def_readwrite("model_parts", &nw::Item::model_parts);
}

void init_objects_module(py::module& nw)
{
    py::class_<nw::ModuleScripts>(nw, "ModuleScripts")
        .def_readwrite("on_client_enter", &nw::ModuleScripts::on_client_enter)
        .def_readwrite("on_client_leave", &nw::ModuleScripts::on_client_leave)
        .def_readwrite("on_cutsnabort", &nw::ModuleScripts::on_cutsnabort)
        .def_readwrite("on_heartbeat", &nw::ModuleScripts::on_heartbeat)
        .def_readwrite("on_item_acquire", &nw::ModuleScripts::on_item_acquire)
        .def_readwrite("on_item_activate", &nw::ModuleScripts::on_item_activate)
        .def_readwrite("on_item_unaquire", &nw::ModuleScripts::on_item_unaquire)
        .def_readwrite("on_load", &nw::ModuleScripts::on_load)
        .def_readwrite("on_player_chat", &nw::ModuleScripts::on_player_chat)
        .def_readwrite("on_player_death", &nw::ModuleScripts::on_player_death)
        .def_readwrite("on_player_dying", &nw::ModuleScripts::on_player_dying)
        .def_readwrite("on_player_equip", &nw::ModuleScripts::on_player_equip)
        .def_readwrite("on_player_level_up", &nw::ModuleScripts::on_player_level_up)
        .def_readwrite("on_player_rest", &nw::ModuleScripts::on_player_rest)
        .def_readwrite("on_player_uneqiup", &nw::ModuleScripts::on_player_uneqiup)
        .def_readwrite("on_spawnbtndn", &nw::ModuleScripts::on_spawnbtndn)
        .def_readwrite("on_start", &nw::ModuleScripts::on_start)
        .def_readwrite("on_user_defined", &nw::ModuleScripts::on_user_defined);

    py::class_<nw::Module, nw::ObjectBase>(nw, "Module")
        .def(py::init<>())

        .def("to_dict", [](const nw::Module& self) {
            nlohmann::json j;
            nw::Module::serialize(&self, j);
            return j;
        })

        .def_readonly_static("json_archive_version", &nw::Module::json_archive_version)
        .def_readonly_static("object_type", &nw::Module::object_type)

        .def("area_count", &nw::Module::area_count)
        .def("get_area", &nw::Module::get_area, py::return_value_policy::reference_internal)
        .def("__len__", [](const nw::Module& self) {
            return self.area_count();
        })
        .def(
            "__iter__", [](nw::Module& self) {
                auto& areas = std::get<std::vector<nw::Area*>>(self.areas);
                return py::make_iterator(areas.begin(), areas.end());
            },
            py::keep_alive<0, 1>())

        .def_readwrite("description", &nw::Module::description)
        .def_readwrite("entry_area", &nw::Module::entry_area)
        .def_readwrite("entry_orientation", &nw::Module::entry_orientation)
        .def_readwrite("entry_position", &nw::Module::entry_position)
        .def_readwrite("haks", &nw::Module::haks)
        .def_readwrite("id", &nw::Module::id)
        .def_readwrite("locals", &nw::Module::locals)
        .def_readwrite("min_game_version", &nw::Module::min_game_version)
        .def_readwrite("name", &nw::Module::name)
        .def_readwrite("scripts", &nw::Module::scripts)
        .def_readwrite("start_movie", &nw::Module::start_movie)
        .def_readwrite("tag", &nw::Module::tag)
        .def_readwrite("tlk", &nw::Module::tlk)

        .def_readwrite("creator", &nw::Module::creator)
        .def_readwrite("start_year", &nw::Module::start_year)
        .def_readwrite("version", &nw::Module::version)

        .def_readwrite("expansion_pack", &nw::Module::expansion_pack)

        .def_readwrite("dawn_hour", &nw::Module::dawn_hour)
        .def_readwrite("dusk_hour", &nw::Module::dusk_hour)
        .def_readwrite("is_save_game", &nw::Module::is_save_game)
        .def_readwrite("minutes_per_hour", &nw::Module::minutes_per_hour)
        .def_readwrite("start_day", &nw::Module::start_day)
        .def_readwrite("start_hour", &nw::Module::start_hour)
        .def_readwrite("start_month", &nw::Module::start_month)
        .def_readwrite("xpscale", &nw::Module::xpscale);
}

void init_objects_placeable(py::module& nw)
{
    py::enum_<nw::PlaceableAnimationState>(nw, "PlaceableAnimationState")
        .value("none", nw::PlaceableAnimationState::none)
        .value("open", nw::PlaceableAnimationState::open)
        .value("closed", nw::PlaceableAnimationState::closed)
        .value("destroyed", nw::PlaceableAnimationState::destroyed)
        .value("activated", nw::PlaceableAnimationState::activated)
        .value("deactivated", nw::PlaceableAnimationState::deactivated);

    py::class_<nw::PlaceableScripts>(nw, "PlaceableScripts")
        .def_readwrite("on_click", &nw::PlaceableScripts::on_click)
        .def_readwrite("on_closed", &nw::PlaceableScripts::on_closed)
        .def_readwrite("on_damaged", &nw::PlaceableScripts::on_damaged)
        .def_readwrite("on_death", &nw::PlaceableScripts::on_death)
        .def_readwrite("on_disarm", &nw::PlaceableScripts::on_disarm)
        .def_readwrite("on_heartbeat", &nw::PlaceableScripts::on_heartbeat)
        .def_readwrite("on_inventory_disturbed", &nw::PlaceableScripts::on_inventory_disturbed)
        .def_readwrite("on_lock", &nw::PlaceableScripts::on_lock)
        .def_readwrite("on_melee_attacked", &nw::PlaceableScripts::on_melee_attacked)
        .def_readwrite("on_open", &nw::PlaceableScripts::on_open)
        .def_readwrite("on_spell_cast_at", &nw::PlaceableScripts::on_spell_cast_at)
        .def_readwrite("on_trap_triggered", &nw::PlaceableScripts::on_trap_triggered)
        .def_readwrite("on_unlock", &nw::PlaceableScripts::on_unlock)
        .def_readwrite("on_used", &nw::PlaceableScripts::on_used)
        .def_readwrite("on_user_defined", &nw::PlaceableScripts::on_user_defined);

    py::class_<nw::Placeable, nw::ObjectBase>(nw, "Placeable")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Placeable>)

        .def_readonly_static("json_archive_version", &nw::Placeable::json_archive_version)
        .def_readonly_static("object_type", &nw::Placeable::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Placeable>)
        .def_static("from_file", &create_object_from_file_helper<nw::Placeable>)

        .def_readwrite("common", &nw::Placeable::common)
        .def_readwrite("conversation", &nw::Placeable::conversation)
        .def_readwrite("description", &nw::Placeable::description)
        .def_property_readonly(
            "inventory", [](const nw::Placeable& s) { return &s.inventory; }, py::return_value_policy::reference_internal)
        .def_readwrite("lock", &nw::Placeable::lock)
        .def_readwrite("saves", &nw::Placeable::saves)
        .def_readwrite("scripts", &nw::Placeable::scripts)
        .def_readwrite("trap", &nw::Placeable::trap)

        .def_readwrite("appearance", &nw::Placeable::appearance)
        .def_readwrite("faction", &nw::Placeable::faction)

        .def_readwrite("hp", &nw::Placeable::hp)
        .def_readwrite("hp_current", &nw::Placeable::hp_current)
        .def_readwrite("portrait_id", &nw::Placeable::portrait_id)

        .def_readwrite("animation_state", &nw::Placeable::animation_state)
        .def_readwrite("bodybag", &nw::Placeable::bodybag)
        .def_readwrite("hardness", &nw::Placeable::hardness)
        .def_readwrite("has_inventory", &nw::Placeable::has_inventory)
        .def_readwrite("interruptable", &nw::Placeable::interruptable)
        .def_readwrite("plot", &nw::Placeable::plot)
        .def_readwrite("static", &nw::Placeable::static_)
        .def_readwrite("useable", &nw::Placeable::useable);
}

void init_objects_player(py::module& nw)
{
    py::class_<nw::Player, nw::Creature>(nw, "Player")
        .def_readonly_static("json_archive_version", &nw::Player::json_archive_version)
        .def_readonly_static("object_type", &nw::Player::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Player>)
        .def_static("from_file", &create_object_from_file_helper<nw::Player>);
}

void init_objects_sound(py::module& nw)
{
    py::class_<nw::Sound, nw::ObjectBase>(nw, "Sound")
        .def(py::init<>())

        .def("to_dict", &to_json_helper<nw::Sound>)

        .def_readonly_static("json_archive_version", &nw::Sound::json_archive_version)
        .def_readonly_static("object_type", &nw::Sound::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Sound>)
        .def_static("from_file", &create_object_from_file_helper<nw::Sound>)

        .def_readwrite("common", &nw::Sound::common)
        .def_readwrite("sounds", &nw::Sound::sounds)

        .def_readwrite("distance_min", &nw::Sound::distance_min)
        .def_readwrite("distance_max", &nw::Sound::distance_max)
        .def_readwrite("elevation", &nw::Sound::elevation)
        .def_readwrite("generated_type", &nw::Sound::generated_type)
        .def_readwrite("hours", &nw::Sound::hours)
        .def_readwrite("interval", &nw::Sound::interval)
        .def_readwrite("interval_variation", &nw::Sound::interval_variation)
        .def_readwrite("pitch_variation", &nw::Sound::pitch_variation)
        .def_readwrite("random_x", &nw::Sound::random_x)
        .def_readwrite("random_y", &nw::Sound::random_y)

        .def_readwrite("active", &nw::Sound::active)
        .def_readwrite("continuous", &nw::Sound::continuous)
        .def_readwrite("looping", &nw::Sound::looping)
        .def_readwrite("positional", &nw::Sound::positional)
        .def_readwrite("priority", &nw::Sound::priority)
        .def_readwrite("random", &nw::Sound::random)
        .def_readwrite("random_position", &nw::Sound::random_position)
        .def_readwrite("times", &nw::Sound::times)
        .def_readwrite("volume", &nw::Sound::volume)
        .def_readwrite("volume_variation", &nw::Sound::volume_variation);
}

void init_objects_store(py::module& nw)
{
    py::class_<nw::StoreScripts>(nw, "StoreScripts")
        .def_readwrite("on_closed", &nw::StoreScripts::on_closed)
        .def_readwrite("on_opened", &nw::StoreScripts::on_opened);

    py::class_<nw::Store, nw::ObjectBase>(nw, "Store")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Store>)

        .def_readonly_static("json_archive_version", &nw::Store::json_archive_version)
        .def_readonly_static("object_type", &nw::Store::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Store>)
        .def_static("from_file", &create_object_from_file_helper<nw::Store>)

        .def_readwrite("common", &nw::Store::common)

        .def_property_readonly(
            "armor", [](const nw::Store& s) { return &s.inventory.armor; }, py::return_value_policy::reference_internal)
        .def_property_readonly(
            "miscellaneous", [](const nw::Store& s) { return &s.inventory.miscellaneous; }, py::return_value_policy::reference_internal)
        .def_property_readonly(
            "potions", [](const nw::Store& s) { return &s.inventory.potions; }, py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rings", [](const nw::Store& s) { return &s.inventory.rings; }, py::return_value_policy::reference_internal)
        .def_property_readonly(
            "weapons", [](const nw::Store& s) { return &s.inventory.weapons; }, py::return_value_policy::reference_internal)
        .def_readonly("scripts", &nw::Store::scripts)
        //.def_readonly("will_not_buy", &nw::Store::will_not_buy)
        //.def_readonly("will_only_buy", &nw::Store::will_only_buy)

        .def_readwrite("blackmarket_markdown", &nw::Store::blackmarket_markdown)
        .def_readwrite("identify_price", &nw::Store::identify_price)
        .def_readwrite("markdown", &nw::Store::markdown)
        .def_readwrite("markup", &nw::Store::markup)
        .def_readwrite("max_price", &nw::Store::max_price)
        .def_readwrite("gold", &nw::Store::gold)

        .def_readwrite("blackmarket", &nw::Store::blackmarket);
}

void init_object_trigger(pybind11::module& nw)
{
    pybind11::class_<nw::TriggerScripts>(nw, "TriggerScripts")
        .def_readwrite("on_click", &nw::TriggerScripts::on_click)
        .def_readwrite("on_disarm", &nw::TriggerScripts::on_disarm)
        .def_readwrite("on_enter", &nw::TriggerScripts::on_enter)
        .def_readwrite("on_exit", &nw::TriggerScripts::on_exit)
        .def_readwrite("on_heartbeat", &nw::TriggerScripts::on_heartbeat)
        .def_readwrite("on_trap_triggered", &nw::TriggerScripts::on_trap_triggered)
        .def_readwrite("on_user_defined", &nw::TriggerScripts::on_user_defined);

    pybind11::class_<nw::Trigger, nw::ObjectBase>(nw, "Trigger")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Trigger>)

        .def_readonly_static("json_archive_version", &nw::Trigger::json_archive_version)
        .def_readonly_static("object_type", &nw::Trigger::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Trigger>)
        .def_static("from_file", &create_object_from_file_helper<nw::Trigger>)

        .def_readwrite("common", &nw::Trigger::common)
        .def_readwrite("geometry", &nw::Trigger::geometry)
        .def_readwrite("linked_to", &nw::Trigger::linked_to)
        .def_readwrite("scripts", &nw::Trigger::scripts)
        .def_readwrite("trap", &nw::Trigger::trap)

        .def_readwrite("faction", &nw::Trigger::faction)
        .def_readwrite("highlight_height", &nw::Trigger::highlight_height)
        .def_readwrite("type", &nw::Trigger::type)

        .def_readwrite("loadscreen", &nw::Trigger::loadscreen)
        .def_readwrite("portrait", &nw::Trigger::portrait)

        .def_readwrite("cursor", &nw::Trigger::cursor)
        .def_readwrite("linked_to_flags", &nw::Trigger::linked_to_flags);
}

void init_object_waypoint(pybind11::module& nw)
{
    pybind11::class_<nw::Waypoint, nw::ObjectBase>(nw, "Waypoint")
        .def(py::init<>())

        .def_readonly_static("json_archive_version", &nw::Waypoint::json_archive_version)
        .def_readonly_static("object_type", &nw::Waypoint::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Waypoint>)
        .def_static("from_file", &create_object_from_file_helper<nw::Waypoint>)

        .def("to_dict", &to_json_helper<nw::Waypoint>)

        .def_readwrite("common", &nw::Waypoint::common)
        .def_readwrite("description", &nw::Waypoint::description)
        .def_readwrite("linked_to", &nw::Waypoint::linked_to)
        .def_readwrite("map_note", &nw::Waypoint::map_note)

        .def_readwrite("appearance", &nw::Waypoint::appearance)
        .def_readwrite("has_map_note", &nw::Waypoint::has_map_note)
        .def_readwrite("map_note_enabled", &nw::Waypoint::map_note_enabled);
}

void init_objects(py::module& nw)
{
    init_object_components(nw);
    init_objects_base(nw);
    init_objects_area(nw);
    init_objects_creature(nw);
    init_objects_door(nw);
    init_objects_encounter(nw);
    init_objects_item(nw);
    init_objects_module(nw);
    init_objects_placeable(nw);
    init_objects_player(nw);
    init_objects_sound(nw);
    init_objects_store(nw);
    init_object_trigger(nw);
    init_object_waypoint(nw);
}
