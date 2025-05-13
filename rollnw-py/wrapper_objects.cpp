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
#include <nw/serialization/GffBuilder.hpp>
#include <nw/util/platform.hpp>
#include <nw/util/string.hpp>
#include <nw/util/templates.hpp>

#include <fmt/format.h>
#include <nanobind/make_iterator.h>
#include <nanobind/nanobind.h>

#include "json/json.hpp"
#include <nanobind/stl/filesystem.h>

namespace py = nanobind;
namespace fs = std::filesystem;

template <typename T>
nlohmann::json to_json_helper(const T* self, nw::SerializationProfile profile)
{
    nlohmann::json result;
    nw::serialize(self, result, profile);
    return result;
}

nlohmann::json to_json_helper_player(const nw::Player* self)
{
    nlohmann::json result;
    nw::serialize(self, result);
    return result;
}

template <typename T>
T* create_object_from_json_helper(const nlohmann::json& j)
{
    auto obj = nw::kernel::objects().make<T>();
    if constexpr (!std::is_same_v<T, nw::Player>) {
        nw::deserialize(obj, j, nw::SerializationProfile::blueprint);
    } else {
        nw::deserialize(obj, j);
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

    auto obj = nw::kernel::objects().load_file<T>(path);
    if (!obj) {
        throw std::runtime_error(fmt::format("failed to load object from file: '{}'", path));
    }
    return obj;
}

void init_object_components(py::module_& nw);

void init_objects_base(py::module_& nw)
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
        .def("__repr__", [](const nw::ObjectHandle& self) {
            return fmt::format("<ObjectHandle id: {}, type: {}",
                nw::to_underlying(self.id), nw::to_underlying(self.type));
        })
        .def_prop_ro("id", [](const nw::ObjectHandle& self) { return self.id; })
        .def_prop_ro("type", [](const nw::ObjectHandle& self) { return self.type; });

    py::class_<nw::ObjectBase>(nw, "ObjectBase")
        .def("handle", &nw::ObjectBase::handle)
        .def("instantiate", &nw::ObjectBase::instantiate)
        //.def("valid", &nw::ObjectBase::valid)
        .def("as_common", py::overload_cast<>(&nw::ObjectBase::as_common), py::rv_policy::reference_internal)
        .def("as_common", py::overload_cast<>(&nw::ObjectBase::as_common, py::const_), py::rv_policy::reference_internal)
        .def("as_creature", py::overload_cast<>(&nw::ObjectBase::as_creature), py::rv_policy::reference_internal)
        .def("as_creature", py::overload_cast<>(&nw::ObjectBase::as_creature, py::const_), py::rv_policy::reference_internal)
        .def("as_door", py::overload_cast<>(&nw::ObjectBase::as_door), py::rv_policy::reference_internal)
        .def("as_door", py::overload_cast<>(&nw::ObjectBase::as_door, py::const_), py::rv_policy::reference_internal)
        .def("as_encounter", py::overload_cast<>(&nw::ObjectBase::as_encounter), py::rv_policy::reference_internal)
        .def("as_encounter", py::overload_cast<>(&nw::ObjectBase::as_encounter, py::const_), py::rv_policy::reference_internal)
        .def("as_item", py::overload_cast<>(&nw::ObjectBase::as_item), py::rv_policy::reference_internal)
        .def("as_item", py::overload_cast<>(&nw::ObjectBase::as_item, py::const_), py::rv_policy::reference_internal)
        .def("as_module", py::overload_cast<>(&nw::ObjectBase::as_module), py::rv_policy::reference_internal)
        .def("as_module", py::overload_cast<>(&nw::ObjectBase::as_module, py::const_), py::rv_policy::reference_internal)
        .def("as_placeable", py::overload_cast<>(&nw::ObjectBase::as_placeable), py::rv_policy::reference_internal)
        .def("as_placeable", py::overload_cast<>(&nw::ObjectBase::as_placeable, py::const_), py::rv_policy::reference_internal)
        .def("as_sound", py::overload_cast<>(&nw::ObjectBase::as_sound), py::rv_policy::reference_internal)
        .def("as_sound", py::overload_cast<>(&nw::ObjectBase::as_sound, py::const_), py::rv_policy::reference_internal)
        .def("as_trigger", py::overload_cast<>(&nw::ObjectBase::as_trigger), py::rv_policy::reference_internal)
        .def("as_trigger", py::overload_cast<>(&nw::ObjectBase::as_trigger, py::const_), py::rv_policy::reference_internal)
        .def("as_waypoint", py::overload_cast<>(&nw::ObjectBase::as_waypoint), py::rv_policy::reference_internal)
        .def("as_waypoint", py::overload_cast<>(&nw::ObjectBase::as_waypoint, py::const_), py::rv_policy::reference_internal);
}

void init_objects_area(py::module_& nw)
{
    py::enum_<nw::AreaFlags>(nw, "AreaFlags")
        .value("none", nw::AreaFlags::none)
        .value("interior", nw::AreaFlags::interior)
        .value("underground", nw::AreaFlags::underground)
        .value("natural", nw::AreaFlags::natural);

    py::class_<nw::AreaScripts>(nw, "AreaScripts")
        .def(py::init<>())
        .def_rw("on_enter", &nw::AreaScripts::on_enter)
        .def_rw("on_exit", &nw::AreaScripts::on_exit)
        .def_rw("on_heartbeat", &nw::AreaScripts::on_heartbeat)
        .def_rw("on_user_defined", &nw::AreaScripts::on_user_defined);

    py::class_<nw::AreaWeather>(nw, "AreaWeather")
        .def(py::init<>())
        .def_rw("chance_lightning", &nw::AreaWeather::chance_lightning)
        .def_rw("chance_rain", &nw::AreaWeather::chance_rain)
        .def_rw("chance_snow", &nw::AreaWeather::chance_snow)
        .def_rw("color_moon_ambient", &nw::AreaWeather::color_moon_ambient)
        .def_rw("color_moon_diffuse", &nw::AreaWeather::color_moon_diffuse)
        .def_rw("color_moon_fog", &nw::AreaWeather::color_moon_fog)
        .def_rw("color_sun_ambient", &nw::AreaWeather::color_sun_ambient)
        .def_rw("color_sun_diffuse", &nw::AreaWeather::color_sun_diffuse)
        .def_rw("color_sun_fog", &nw::AreaWeather::color_sun_fog)
        .def_rw("fog_clip_distance", &nw::AreaWeather::fog_clip_distance)
        .def_rw("wind_power", &nw::AreaWeather::wind_power)
        .def_rw("day_night_cycle", &nw::AreaWeather::day_night_cycle)
        .def_rw("is_night", &nw::AreaWeather::is_night)
        .def_rw("lighting_scheme", &nw::AreaWeather::lighting_scheme)
        .def_rw("fog_moon_amount", &nw::AreaWeather::fog_moon_amount)
        .def_rw("moon_shadows", &nw::AreaWeather::moon_shadows)
        .def_rw("fog_sun_amount", &nw::AreaWeather::fog_sun_amount)
        .def_rw("sun_shadows", &nw::AreaWeather::sun_shadows);

    py::class_<nw::AreaTile>(nw, "AreaTile")
        .def(py::init<>())
        .def_rw("id", &nw::AreaTile::id)
        .def_rw("height", &nw::AreaTile::height)
        .def_rw("orientation", &nw::AreaTile::orientation)
        .def_rw("animloop1", &nw::AreaTile::animloop1)
        .def_rw("animloop2", &nw::AreaTile::animloop2)
        .def_rw("animloop3", &nw::AreaTile::animloop3)
        .def_rw("mainlight1", &nw::AreaTile::mainlight1)
        .def_rw("mainlight2", &nw::AreaTile::mainlight2)
        .def_rw("srclight1", &nw::AreaTile::srclight1)
        .def_rw("srclight2", &nw::AreaTile::srclight2);

    py::class_<nw::Area, nw::ObjectBase>(nw, "Area")
        .def(py::init<>())

        .def_ro_static("json_archive_version", &nw::Area::json_archive_version)
        .def_ro_static("object_type", &nw::Area::object_type)

        .def_ro("creatures", &nw::Area::creatures)
        .def_ro("doors", &nw::Area::doors)
        .def_ro("encounters", &nw::Area::encounters)
        .def_ro("items", &nw::Area::items)
        .def_ro("placeables", &nw::Area::placeables)
        .def_ro("sounds", &nw::Area::sounds)
        .def_ro("stores", &nw::Area::stores)
        .def_ro("triggers", &nw::Area::triggers)
        .def_ro("waypoints", &nw::Area::waypoints)
        .def_rw("comments", &nw::Area::comments)
        .def_rw("name", &nw::Area::name)
        .def_ro("scripts", &nw::Area::scripts)
        .def_rw("tileset_resref", &nw::Area::tileset_resref)
        .def_ro("tiles", &nw::Area::tiles)
        .def_rw("weather", &nw::Area::weather)
        .def_rw("creator_id", &nw::Area::creator_id)
        .def_rw("flags", &nw::Area::flags)
        .def_rw("height", &nw::Area::height)
        .def_rw("id", &nw::Area::id)
        .def_rw("listen_check_mod", &nw::Area::listen_check_mod)
        .def_rw("spot_check_mod", &nw::Area::spot_check_mod)
        .def_rw("version", &nw::Area::version)
        .def_rw("width", &nw::Area::width)
        .def_rw("loadscreen", &nw::Area::loadscreen)
        .def_rw("no_rest", &nw::Area::no_rest)
        .def_rw("pvp", &nw::Area::pvp)
        .def_rw("shadow_opacity", &nw::Area::shadow_opacity)
        .def_rw("skybox", &nw::Area::skybox);
}

void init_objects_creature(py::module_& nw)
{
    py::class_<nw::CreatureScripts>(nw, "CreatureScripts")
        .def_rw("on_attacked", &nw::CreatureScripts::on_attacked)
        .def_rw("on_blocked", &nw::CreatureScripts::on_blocked)
        .def_rw("on_conversation", &nw::CreatureScripts::on_conversation)
        .def_rw("on_damaged", &nw::CreatureScripts::on_damaged)
        .def_rw("on_death", &nw::CreatureScripts::on_death)
        .def_rw("on_disturbed", &nw::CreatureScripts::on_disturbed)
        .def_rw("on_endround", &nw::CreatureScripts::on_endround)
        .def_rw("on_heartbeat", &nw::CreatureScripts::on_heartbeat)
        .def_rw("on_perceived", &nw::CreatureScripts::on_perceived)
        .def_rw("on_rested", &nw::CreatureScripts::on_rested)
        .def_rw("on_spawn", &nw::CreatureScripts::on_spawn)
        .def_rw("on_spell_cast_at", &nw::CreatureScripts::on_spell_cast_at)
        .def_rw("on_user_defined", &nw::CreatureScripts::on_user_defined);

    py::class_<nw::Creature, nw::ObjectBase>(nw, "Creature")
        .def(py::init<>())

        .def_static("from_dict", &create_object_from_json_helper<nw::Creature>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Creature>, py::rv_policy::reference)

        .def("to_dict", &to_json_helper<nw::Creature>)
        .def("handle", &nw::Creature::handle)

        .def_ro_static("json_archive_version", &nw::Creature::json_archive_version)
        .def_ro_static("object_type", &nw::Creature::object_type)

        .def("save", &nw::Creature::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Creature::common)
        .def_rw("appearance", &nw::Creature::appearance)
        //.def_rw("combat_info", &nw::Creature::combat_info)
        .def_rw("conversation", &nw::Creature::conversation)
        .def_rw("deity", &nw::Creature::deity)
        .def_rw("description", &nw::Creature::description)
        .def_prop_ro(
            "equipment", [](const nw::Creature& self) { return &self.equipment; }, py::rv_policy::reference_internal)
        .def_prop_ro(
            "history", [](const nw::Creature& self) { return &self.history; }, py::rv_policy::reference_internal)
        .def_prop_ro(
            "inventory", [](const nw::Creature& c) { return &c.inventory; }, py::rv_policy::reference_internal)
        .def_rw("levels", &nw::Creature::levels)
        .def_rw("name_first", &nw::Creature::name_first)
        .def_rw("name_last", &nw::Creature::name_last)
        .def_rw("scripts", &nw::Creature::scripts)
        .def_ro("stats", &nw::Creature::stats)
        .def_rw("subrace", &nw::Creature::subrace)

        .def_rw("cr", &nw::Creature::cr)
        .def_rw("cr_adjust", &nw::Creature::cr_adjust)
        .def_rw("decay_time", &nw::Creature::decay_time)
        .def_rw("walkrate", &nw::Creature::walkrate)

        .def_rw("faction_id", &nw::Creature::faction_id)
        .def_rw("hp", &nw::Creature::hp)
        .def_rw("hp_current", &nw::Creature::hp_current)
        .def_rw("hp_max", &nw::Creature::hp_max)
        .def_rw("soundset", &nw::Creature::soundset)

        .def_rw("bodybag", &nw::Creature::bodybag)
        .def_rw("chunk_death", &nw::Creature::chunk_death)
        .def_rw("disarmable", &nw::Creature::disarmable)
        .def_rw("gender", &nw::Creature::gender)
        .def_rw("good_evil", &nw::Creature::good_evil)
        .def_rw("immortal", &nw::Creature::immortal)
        .def_rw("interruptable", &nw::Creature::interruptable)
        .def_rw("lawful_chaotic", &nw::Creature::lawful_chaotic)
        .def_rw("lootable", &nw::Creature::lootable)
        .def_rw("pc", &nw::Creature::pc)
        .def_rw("perception_range", &nw::Creature::perception_range)
        .def_rw("plot", &nw::Creature::plot)
        .def_rw("race", &nw::Creature::race)
        .def_rw("starting_package", &nw::Creature::starting_package);
}

void init_objects_door(py::module_& nw)
{
    py::enum_<nw::DoorAnimationState>(nw, "DoorAnimationState")
        .value("closed", nw::DoorAnimationState::closed)
        .value("opened1", nw::DoorAnimationState::opened1)
        .value("opened2", nw::DoorAnimationState::opened2);

    py::class_<nw::DoorScripts>(nw, "DoorScripts")
        .def_rw("on_click", &nw::DoorScripts::on_click)
        .def_rw("on_closed", &nw::DoorScripts::on_closed)
        .def_rw("on_damaged", &nw::DoorScripts::on_damaged)
        .def_rw("on_death", &nw::DoorScripts::on_death)
        .def_rw("on_disarm", &nw::DoorScripts::on_disarm)
        .def_rw("on_heartbeat", &nw::DoorScripts::on_heartbeat)
        .def_rw("on_lock", &nw::DoorScripts::on_lock)
        .def_rw("on_melee_attacked", &nw::DoorScripts::on_melee_attacked)
        .def_rw("on_open_failure", &nw::DoorScripts::on_open_failure)
        .def_rw("on_open", &nw::DoorScripts::on_open)
        .def_rw("on_spell_cast_at", &nw::DoorScripts::on_spell_cast_at)
        .def_rw("on_trap_triggered", &nw::DoorScripts::on_trap_triggered)
        .def_rw("on_unlock", &nw::DoorScripts::on_unlock)
        .def_rw("on_user_defined", &nw::DoorScripts::on_user_defined);

    py::class_<nw::Door, nw::ObjectBase>(nw, "Door")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Door>)

        .def_ro_static("json_archive_version", &nw::Door::json_archive_version)
        .def_ro_static("object_type", &nw::Door::object_type)

        .def("save", &nw::Door::save, py::arg("path"), py::arg("format") = "json")

        .def_static("from_dict", &create_object_from_json_helper<nw::Door>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Door>, py::rv_policy::reference)

        .def_rw("common", &nw::Door::common)
        .def_rw("conversation", &nw::Door::conversation)
        .def_rw("description", &nw::Door::description)
        .def_rw("linked_to", &nw::Door::linked_to)
        .def_rw("lock", &nw::Door::lock)
        .def_rw("saves", &nw::Door::saves)
        .def_rw("scripts", &nw::Door::scripts)
        .def_rw("trap", &nw::Door::trap)

        .def_rw("appearance", &nw::Door::appearance)
        .def_rw("faction", &nw::Door::faction)
        .def_rw("generic_type", &nw::Door::generic_type)

        .def_rw("hp", &nw::Door::hp)
        .def_rw("hp_current", &nw::Door::hp_current)
        .def_rw("loadscreen", &nw::Door::loadscreen)
        .def_rw("portrait_id", &nw::Door::portrait_id)

        .def_rw("animation_state", &nw::Door::animation_state)
        .def_rw("hardness", &nw::Door::hardness)
        .def_rw("interruptable", &nw::Door::interruptable)
        .def_rw("linked_to_flags", &nw::Door::linked_to_flags)
        .def_rw("plot", &nw::Door::plot);
}

void init_objects_encounter(py::module_& nw)
{
    py::class_<nw::EncounterScripts>(nw, "EncounterScripts")
        .def_rw("on_entered", &nw::EncounterScripts::on_entered)
        .def_rw("on_exhausted", &nw::EncounterScripts::on_exhausted)
        .def_rw("on_exit", &nw::EncounterScripts::on_exit)
        .def_rw("on_heartbeat", &nw::EncounterScripts::on_heartbeat)
        .def_rw("on_user_defined", &nw::EncounterScripts::on_user_defined);

    py::class_<nw::SpawnCreature>(nw, "SpawnCreature")
        .def_rw("appearance", &nw::SpawnCreature::appearance)
        .def_rw("cr", &nw::SpawnCreature::cr)
        .def_rw("resref", &nw::SpawnCreature::resref)
        .def_rw("single_spawn", &nw::SpawnCreature::single_spawn);

    py::class_<nw::SpawnPoint>(nw, "SpawnPoint")
        .def_rw("orientation", &nw::SpawnPoint::orientation)
        .def_rw("position", &nw::SpawnPoint::position);

    py::class_<nw::Encounter, nw::ObjectBase>(nw, "Encounter")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Encounter>)

        .def_ro_static("json_archive_version", &nw::Encounter::json_archive_version)
        .def_ro_static("object_type", &nw::Encounter::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Encounter>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Encounter>, py::rv_policy::reference)

        .def("save", &nw::Encounter::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Encounter::common)
        .def_rw("creatures", &nw::Encounter::creatures)
        .def_rw("geometry", &nw::Encounter::geometry)
        .def_ro("scripts", &nw::Encounter::scripts)
        .def_rw("spawn_points", &nw::Encounter::spawn_points)

        .def_rw("creatures_max", &nw::Encounter::creatures_max)
        .def_rw("creatures_recommended", &nw::Encounter::creatures_recommended)
        .def_rw("difficulty", &nw::Encounter::difficulty)
        .def_rw("difficulty_index", &nw::Encounter::difficulty_index)
        .def_rw("faction", &nw::Encounter::faction)
        .def_rw("reset_time", &nw::Encounter::reset_time)
        .def_rw("respawns", &nw::Encounter::respawns)
        .def_rw("spawn_option", &nw::Encounter::spawn_option)

        .def_rw("active", &nw::Encounter::active)
        .def_rw("player_only", &nw::Encounter::player_only)
        .def_rw("reset", &nw::Encounter::reset);
}

void init_objects_item(py::module_& nw)
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
        .def_rw("type", &nw::ItemProperty::type)
        .def_rw("subtype", &nw::ItemProperty::subtype)
        .def_rw("cost_table", &nw::ItemProperty::cost_table)
        .def_rw("cost_value", &nw::ItemProperty::cost_value)
        .def_rw("param_table", &nw::ItemProperty::param_table)
        .def_rw("param_value", &nw::ItemProperty::param_value);

    py::class_<nw::Item, nw::ObjectBase>(nw, "Item")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Item>)
        .def("handle", &nw::Item::handle)
        .def("model_to_plt_colors", &nw::Item::model_to_plt_colors)
        .def("get_icon_by_part", &nw::Item::get_icon_by_part,
            py::arg("part") = nw::ItemModelParts::model1, py::arg("female") = false)

        .def_ro_static("json_archive_version", &nw::Item::json_archive_version)
        .def_ro_static("object_type", &nw::Item::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Item>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Item>, py::rv_policy::reference)

        .def("save", &nw::Item::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Item::common)

        .def_rw("description", &nw::Item::description)
        .def_rw("description_id", &nw::Item::description_id)
        .def_prop_ro(
            "inventory", [](const nw::Item& i) { return &i.inventory; }, py::rv_policy::reference_internal)
        .def_rw("properties", &nw::Item::properties)

        .def_rw("cost", &nw::Item::cost)
        .def_rw("additional_cost", &nw::Item::additional_cost)
        .def_rw("baseitem", &nw::Item::baseitem)

        .def_rw("stacksize", &nw::Item::stacksize)

        .def_rw("charges", &nw::Item::charges)
        .def_rw("cursed", &nw::Item::cursed)
        .def_rw("identified", &nw::Item::identified)
        .def_rw("plot", &nw::Item::plot)
        .def_rw("stolen", &nw::Item::stolen)

        .def_rw("model_type", &nw::Item::model_type)
        .def_rw("model_colors", &nw::Item::model_colors)
        .def_rw("model_parts", &nw::Item::model_parts);
}

void init_objects_module(py::module_& nw)
{
    py::class_<nw::ModuleScripts>(nw, "ModuleScripts")
        .def_rw("on_client_enter", &nw::ModuleScripts::on_client_enter)
        .def_rw("on_client_leave", &nw::ModuleScripts::on_client_leave)
        .def_rw("on_cutsnabort", &nw::ModuleScripts::on_cutsnabort)
        .def_rw("on_heartbeat", &nw::ModuleScripts::on_heartbeat)
        .def_rw("on_item_acquire", &nw::ModuleScripts::on_item_acquire)
        .def_rw("on_item_activate", &nw::ModuleScripts::on_item_activate)
        .def_rw("on_item_unaquire", &nw::ModuleScripts::on_item_unaquire)
        .def_rw("on_load", &nw::ModuleScripts::on_load)
        .def_rw("on_player_chat", &nw::ModuleScripts::on_player_chat)
        .def_rw("on_player_death", &nw::ModuleScripts::on_player_death)
        .def_rw("on_player_dying", &nw::ModuleScripts::on_player_dying)
        .def_rw("on_player_equip", &nw::ModuleScripts::on_player_equip)
        .def_rw("on_player_level_up", &nw::ModuleScripts::on_player_level_up)
        .def_rw("on_player_rest", &nw::ModuleScripts::on_player_rest)
        .def_rw("on_player_uneqiup", &nw::ModuleScripts::on_player_uneqiup)
        .def_rw("on_spawnbtndn", &nw::ModuleScripts::on_spawnbtndn)
        .def_rw("on_start", &nw::ModuleScripts::on_start)
        .def_rw("on_user_defined", &nw::ModuleScripts::on_user_defined);

    py::class_<nw::Module, nw::ObjectBase>(nw, "Module")
        .def(py::init<>())

        .def("to_dict", [](const nw::Module& self) {
            nlohmann::json j;
            nw::Module::serialize(&self, j);
            return j;
        })

        .def_ro_static("json_archive_version", &nw::Module::json_archive_version)
        .def_ro_static("object_type", &nw::Module::object_type)

        .def("area_count", &nw::Module::area_count)
        .def("get_area", [](nw::Module& self, size_t index) { return self.get_area(index); }, py::rv_policy::reference_internal)
        .def("__len__", [](const nw::Module& self) { return self.area_count(); })
        .def("__iter__", [](nw::Module& self) {
                auto& areas = self.areas.as<std::vector<nw::Area*>>();
                return py::make_iterator(py::type<std::vector<nw::Area*>>(), "AreaIterator", areas.begin(), areas.end()); }, py::keep_alive<0, 1>())

        .def_rw("description", &nw::Module::description)
        .def_rw("entry_area", &nw::Module::entry_area)
        .def_rw("entry_orientation", &nw::Module::entry_orientation)
        .def_rw("entry_position", &nw::Module::entry_position)
        .def_rw("haks", &nw::Module::haks)
        .def_rw("id", &nw::Module::id)
        .def_rw("locals", &nw::Module::locals)
        .def_rw("min_game_version", &nw::Module::min_game_version)
        .def_rw("name", &nw::Module::name)
        .def_rw("scripts", &nw::Module::scripts)
        .def_rw("start_movie", &nw::Module::start_movie)
        .def_rw("tag", &nw::Module::tag)
        .def_rw("tlk", &nw::Module::tlk)
        .def_prop_ro("uuid", [](const nw::Module& self) { return uuids::to_string(self.uuid); })

        .def_rw("creator", &nw::Module::creator)
        .def_rw("start_year", &nw::Module::start_year)
        .def_rw("version", &nw::Module::version)

        .def_rw("expansion_pack", &nw::Module::expansion_pack)

        .def_rw("dawn_hour", &nw::Module::dawn_hour)
        .def_rw("dusk_hour", &nw::Module::dusk_hour)
        .def_rw("is_save_game", &nw::Module::is_save_game)
        .def_rw("minutes_per_hour", &nw::Module::minutes_per_hour)
        .def_rw("start_day", &nw::Module::start_day)
        .def_rw("start_hour", &nw::Module::start_hour)
        .def_rw("start_month", &nw::Module::start_month)
        .def_rw("xpscale", &nw::Module::xpscale);
}

void init_objects_placeable(py::module_& nw)
{
    py::enum_<nw::PlaceableAnimationState>(nw, "PlaceableAnimationState")
        .value("none", nw::PlaceableAnimationState::none)
        .value("open", nw::PlaceableAnimationState::open)
        .value("closed", nw::PlaceableAnimationState::closed)
        .value("destroyed", nw::PlaceableAnimationState::destroyed)
        .value("activated", nw::PlaceableAnimationState::activated)
        .value("deactivated", nw::PlaceableAnimationState::deactivated);

    py::class_<nw::PlaceableScripts>(nw, "PlaceableScripts")
        .def_rw("on_click", &nw::PlaceableScripts::on_click)
        .def_rw("on_closed", &nw::PlaceableScripts::on_closed)
        .def_rw("on_damaged", &nw::PlaceableScripts::on_damaged)
        .def_rw("on_death", &nw::PlaceableScripts::on_death)
        .def_rw("on_disarm", &nw::PlaceableScripts::on_disarm)
        .def_rw("on_heartbeat", &nw::PlaceableScripts::on_heartbeat)
        .def_rw("on_inventory_disturbed", &nw::PlaceableScripts::on_inventory_disturbed)
        .def_rw("on_lock", &nw::PlaceableScripts::on_lock)
        .def_rw("on_melee_attacked", &nw::PlaceableScripts::on_melee_attacked)
        .def_rw("on_open", &nw::PlaceableScripts::on_open)
        .def_rw("on_spell_cast_at", &nw::PlaceableScripts::on_spell_cast_at)
        .def_rw("on_trap_triggered", &nw::PlaceableScripts::on_trap_triggered)
        .def_rw("on_unlock", &nw::PlaceableScripts::on_unlock)
        .def_rw("on_used", &nw::PlaceableScripts::on_used)
        .def_rw("on_user_defined", &nw::PlaceableScripts::on_user_defined);

    py::class_<nw::Placeable, nw::ObjectBase>(nw, "Placeable")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Placeable>)

        .def_ro_static("json_archive_version", &nw::Placeable::json_archive_version)
        .def_ro_static("object_type", &nw::Placeable::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Placeable>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Placeable>, py::rv_policy::reference)

        .def("save", &nw::Placeable::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Placeable::common)
        .def_rw("conversation", &nw::Placeable::conversation)
        .def_rw("description", &nw::Placeable::description)
        .def_prop_ro(
            "inventory", [](const nw::Placeable& s) { return &s.inventory; }, py::rv_policy::reference_internal)
        .def_rw("lock", &nw::Placeable::lock)
        .def_rw("saves", &nw::Placeable::saves)
        .def_rw("scripts", &nw::Placeable::scripts)
        .def_rw("trap", &nw::Placeable::trap)

        .def_rw("appearance", &nw::Placeable::appearance)
        .def_rw("faction", &nw::Placeable::faction)

        .def_rw("hp", &nw::Placeable::hp)
        .def_rw("hp_current", &nw::Placeable::hp_current)
        .def_rw("portrait_id", &nw::Placeable::portrait_id)

        .def_rw("animation_state", &nw::Placeable::animation_state)
        .def_rw("bodybag", &nw::Placeable::bodybag)
        .def_rw("hardness", &nw::Placeable::hardness)
        .def_rw("has_inventory", &nw::Placeable::has_inventory)
        .def_rw("interruptable", &nw::Placeable::interruptable)
        .def_rw("plot", &nw::Placeable::plot)
        .def_rw("static", &nw::Placeable::static_)
        .def_rw("useable", &nw::Placeable::useable);
}

void init_objects_player(py::module_& nw)
{
    py::class_<nw::Player, nw::Creature>(nw, "Player")
        .def_ro_static("json_archive_version", &nw::Player::json_archive_version)
        .def_ro_static("object_type", &nw::Player::object_type)

        .def("to_dict", &to_json_helper_player)

        .def("save", &nw::Player::save, py::arg("path"), py::arg("format") = "json")

        .def_static("from_dict", &create_object_from_json_helper<nw::Player>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Player>, py::rv_policy::reference);
}

void init_objects_sound(py::module_& nw)
{
    py::class_<nw::Sound, nw::ObjectBase>(nw, "Sound")
        .def(py::init<>())

        .def("to_dict", &to_json_helper<nw::Sound>)

        .def_ro_static("json_archive_version", &nw::Sound::json_archive_version)
        .def_ro_static("object_type", &nw::Sound::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Sound>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Sound>, py::rv_policy::reference)

        .def("save", &nw::Sound::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Sound::common)
        .def_rw("sounds", &nw::Sound::sounds)

        .def_rw("distance_min", &nw::Sound::distance_min)
        .def_rw("distance_max", &nw::Sound::distance_max)
        .def_rw("elevation", &nw::Sound::elevation)
        .def_rw("generated_type", &nw::Sound::generated_type)
        .def_rw("hours", &nw::Sound::hours)
        .def_rw("interval", &nw::Sound::interval)
        .def_rw("interval_variation", &nw::Sound::interval_variation)
        .def_rw("pitch_variation", &nw::Sound::pitch_variation)
        .def_rw("random_x", &nw::Sound::random_x)
        .def_rw("random_y", &nw::Sound::random_y)

        .def_rw("active", &nw::Sound::active)
        .def_rw("continuous", &nw::Sound::continuous)
        .def_rw("looping", &nw::Sound::looping)
        .def_rw("positional", &nw::Sound::positional)
        .def_rw("priority", &nw::Sound::priority)
        .def_rw("random", &nw::Sound::random)
        .def_rw("random_position", &nw::Sound::random_position)
        .def_rw("times", &nw::Sound::times)
        .def_rw("volume", &nw::Sound::volume)
        .def_rw("volume_variation", &nw::Sound::volume_variation);
}

void init_objects_store(py::module_& nw)
{
    py::class_<nw::StoreScripts>(nw, "StoreScripts")
        .def_rw("on_closed", &nw::StoreScripts::on_closed)
        .def_rw("on_opened", &nw::StoreScripts::on_opened);

    py::class_<nw::Store, nw::ObjectBase>(nw, "Store")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Store>)

        .def_ro_static("json_archive_version", &nw::Store::json_archive_version)
        .def_ro_static("object_type", &nw::Store::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Store>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Store>, py::rv_policy::reference)

        .def("save", &nw::Store::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Store::common)

        .def_prop_ro(
            "armor", [](const nw::Store& s) { return &s.inventory.armor; }, py::rv_policy::reference_internal)
        .def_prop_ro(
            "miscellaneous", [](const nw::Store& s) { return &s.inventory.miscellaneous; }, py::rv_policy::reference_internal)
        .def_prop_ro(
            "potions", [](const nw::Store& s) { return &s.inventory.potions; }, py::rv_policy::reference_internal)
        .def_prop_ro(
            "rings", [](const nw::Store& s) { return &s.inventory.rings; }, py::rv_policy::reference_internal)
        .def_prop_ro(
            "weapons", [](const nw::Store& s) { return &s.inventory.weapons; }, py::rv_policy::reference_internal)
        .def_ro("scripts", &nw::Store::scripts)
        //.def_ro("will_not_buy", &nw::Store::will_not_buy)
        //.def_ro("will_only_buy", &nw::Store::will_only_buy)

        .def_rw("blackmarket_markdown", &nw::Store::blackmarket_markdown)
        .def_rw("identify_price", &nw::Store::identify_price)
        .def_rw("markdown", &nw::Store::markdown)
        .def_rw("markup", &nw::Store::markup)
        .def_rw("max_price", &nw::Store::max_price)
        .def_rw("gold", &nw::Store::gold)

        .def_rw("blackmarket", &nw::Store::blackmarket);
}

void init_object_trigger(py::module_& nw)
{
    py::class_<nw::TriggerScripts>(nw, "TriggerScripts")
        .def_rw("on_click", &nw::TriggerScripts::on_click)
        .def_rw("on_disarm", &nw::TriggerScripts::on_disarm)
        .def_rw("on_enter", &nw::TriggerScripts::on_enter)
        .def_rw("on_exit", &nw::TriggerScripts::on_exit)
        .def_rw("on_heartbeat", &nw::TriggerScripts::on_heartbeat)
        .def_rw("on_trap_triggered", &nw::TriggerScripts::on_trap_triggered)
        .def_rw("on_user_defined", &nw::TriggerScripts::on_user_defined);

    py::class_<nw::Trigger, nw::ObjectBase>(nw, "Trigger")
        .def(py::init<>())
        .def("to_dict", &to_json_helper<nw::Trigger>)

        .def_ro_static("json_archive_version", &nw::Trigger::json_archive_version)
        .def_ro_static("object_type", &nw::Trigger::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Trigger>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Trigger>, py::rv_policy::reference)

        .def("save", &nw::Trigger::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Trigger::common)
        .def_rw("geometry", &nw::Trigger::geometry)
        .def_rw("linked_to", &nw::Trigger::linked_to)
        .def_rw("scripts", &nw::Trigger::scripts)
        .def_rw("trap", &nw::Trigger::trap)

        .def_rw("faction", &nw::Trigger::faction)
        .def_rw("highlight_height", &nw::Trigger::highlight_height)
        .def_rw("type", &nw::Trigger::type)

        .def_rw("loadscreen", &nw::Trigger::loadscreen)
        .def_rw("portrait", &nw::Trigger::portrait)

        .def_rw("cursor", &nw::Trigger::cursor)
        .def_rw("linked_to_flags", &nw::Trigger::linked_to_flags);
}

void init_object_waypoint(py::module_& nw)
{
    py::class_<nw::Waypoint, nw::ObjectBase>(nw, "Waypoint")
        .def(py::init<>())

        .def_ro_static("json_archive_version", &nw::Waypoint::json_archive_version)
        .def_ro_static("object_type", &nw::Waypoint::object_type)

        .def_static("from_dict", &create_object_from_json_helper<nw::Waypoint>, py::rv_policy::reference)
        .def_static("from_file", &create_object_from_file_helper<nw::Waypoint>, py::rv_policy::reference)

        .def("to_dict", &to_json_helper<nw::Waypoint>)

        .def("save", &nw::Waypoint::save, py::arg("path"), py::arg("format") = "json")

        .def_rw("common", &nw::Waypoint::common)
        .def_rw("description", &nw::Waypoint::description)
        .def_rw("linked_to", &nw::Waypoint::linked_to)
        .def_rw("map_note", &nw::Waypoint::map_note)

        .def_rw("appearance", &nw::Waypoint::appearance)
        .def_rw("has_map_note", &nw::Waypoint::has_map_note)
        .def_rw("map_note_enabled", &nw::Waypoint::map_note_enabled);
}

void init_objects(py::module_& nw)
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
