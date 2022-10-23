#include "opaque_types.hpp"

#include <nw/components/Appearance.hpp>
#include <nw/components/CombatInfo.hpp>
#include <nw/components/Common.hpp>
#include <nw/components/CreatureStats.hpp>
#include <nw/components/Equips.hpp>
#include <nw/components/Inventory.hpp>
#include <nw/components/Item.hpp>
#include <nw/components/LevelStats.hpp>
#include <nw/components/Location.hpp>
#include <nw/components/Lock.hpp>
#include <nw/components/Saves.hpp>
#include <nw/components/SpellBook.hpp>
#include <nw/components/Trap.hpp>

#include <pybind11/pybind11.h>
#include <pybind11_json/pybind11_json.hpp>

namespace py = pybind11;

void init_component_appearance(py::module& m)
{
    py::class_<nw::BodyParts>(m, "BodyParts")
        .def(py::init<>())
        .def_readwrite("belt", &nw::BodyParts::belt)
        .def_readwrite("bicep_left", &nw::BodyParts::bicep_left)
        .def_readwrite("bicep_right", &nw::BodyParts::bicep_right)
        .def_readwrite("foot_left", &nw::BodyParts::foot_left)
        .def_readwrite("foot_right", &nw::BodyParts::foot_right)
        .def_readwrite("forearm_left", &nw::BodyParts::forearm_left)
        .def_readwrite("forearm_right", &nw::BodyParts::forearm_right)
        .def_readwrite("hand_left", &nw::BodyParts::hand_left)
        .def_readwrite("hand_right", &nw::BodyParts::hand_right)
        .def_readwrite("head", &nw::BodyParts::head)
        .def_readwrite("neck", &nw::BodyParts::neck)
        .def_readwrite("pelvis", &nw::BodyParts::pelvis)
        .def_readwrite("shin_left", &nw::BodyParts::shin_left)
        .def_readwrite("shin_right", &nw::BodyParts::shin_right)
        .def_readwrite("shoulder_left", &nw::BodyParts::shoulder_left)
        .def_readwrite("shoulder_right", &nw::BodyParts::shoulder_right)
        .def_readwrite("thigh_left", &nw::BodyParts::thigh_left)
        .def_readwrite("thigh_right", &nw::BodyParts::thigh_right);

    py::class_<nw::Appearance>(m, "Appearance",
        R"(Class containing creature's appearance

        Attributes:
            body_parts (pynwn.BodyParts): body_parts
            hair (int): hair
            id (int): Index into ``appearance.2da``
            phenotype (int): phenotype
            portrait_id (int): Index into ``portraits.2da``
            skin (int): skin
            tail (int): tail
            tattoo1 (int): tattoo1
            tattoo2 (int): tattoo2
            wings (int): wings
        )")
        .def(py::init<>())
        .def_readwrite("phenotype", &nw::Appearance::phenotype)
        .def_readwrite("tail", &nw::Appearance::tail)
        .def_readwrite("wings", &nw::Appearance::wings)
        .def_readwrite("id", &nw::Appearance::id)
        .def_readwrite("portrait_id", &nw::Appearance::portrait_id)
        .def_readwrite("body_parts", &nw::Appearance::body_parts)
        .def_readwrite("hair", &nw::Appearance::hair)
        .def_readwrite("skin", &nw::Appearance::skin)
        .def_readwrite("tattoo1", &nw::Appearance::tattoo1)
        .def_readwrite("tattoo2", &nw::Appearance::tattoo2);
}

void init_component_combatinfo(py::module& m)
{
    py::class_<nw::CombatInfo>(m, "CombatInfo");
}

void init_component_common(py::module& m)
{
    py::class_<nw::Common>(m, "Common",
        R"(Class containing attributes common to all objects

        Attributes:
            resref (pynwn.Resref): resref
            tag (str): tag
            name (pynwn.LocString): name
            locals (pynwn.LocalData): locals
            location (pynwn.Location): location
            comment (str): comment
            palette_id (int): palette_id
        )")
        .def_readwrite("resref", &nw::Common::resref)
        .def_readwrite("tag", &nw::Common::tag)
        .def_readwrite("name", &nw::Common::name)
        .def_readwrite("locals", &nw::Common::locals)
        .def_readwrite("location", &nw::Common::location)
        .def_readwrite("comment", &nw::Common::comment)
        .def_readwrite("palette_id", &nw::Common::palette_id);
}

void init_component_creature_stats(py::module& m)
{
    py::class_<nw::CreatureStats>(m, "CreatureStats")
        .def_readonly("abilities", &nw::CreatureStats::abilities)
        .def("feats", &nw::CreatureStats::feats)
        .def("add_feat", &nw::CreatureStats::add_feat)
        .def("has_feat", &nw::CreatureStats::has_feat)
        .def_readonly("skills", &nw::CreatureStats::skills)
        .def_readonly("save_bonus", &nw::CreatureStats::save_bonus);
}

void init_component_equips(py::module& m)
{
    py::enum_<nw::EquipSlot>(m, "EquipSlot")
        .value("head", nw::EquipSlot::head)
        .value("chest", nw::EquipSlot::chest)
        .value("boots", nw::EquipSlot::boots)
        .value("arms", nw::EquipSlot::arms)
        .value("righthand", nw::EquipSlot::righthand)
        .value("lefthand", nw::EquipSlot::lefthand)
        .value("cloak", nw::EquipSlot::cloak)
        .value("leftring", nw::EquipSlot::leftring)
        .value("rightring", nw::EquipSlot::rightring)
        .value("neck", nw::EquipSlot::neck)
        .value("belt", nw::EquipSlot::belt)
        .value("arrows", nw::EquipSlot::arrows)
        .value("bullets", nw::EquipSlot::bullets)
        .value("bolts", nw::EquipSlot::bolts)
        .value("creature_left", nw::EquipSlot::creature_left)
        .value("creature_right", nw::EquipSlot::creature_right)
        .value("creature_bite", nw::EquipSlot::creature_bite)
        .value("creature_skin", nw::EquipSlot::creature_skin);

    py::enum_<nw::EquipIndex>(m, "EquipIndex")
        .value("head", nw::EquipIndex::head)
        .value("chest", nw::EquipIndex::chest)
        .value("boots", nw::EquipIndex::boots)
        .value("arms", nw::EquipIndex::arms)
        .value("righthand", nw::EquipIndex::righthand)
        .value("lefthand", nw::EquipIndex::lefthand)
        .value("cloak", nw::EquipIndex::cloak)
        .value("leftring", nw::EquipIndex::leftring)
        .value("rightring", nw::EquipIndex::rightring)
        .value("neck", nw::EquipIndex::neck)
        .value("belt", nw::EquipIndex::belt)
        .value("arrows", nw::EquipIndex::arrows)
        .value("bullets", nw::EquipIndex::bullets)
        .value("bolts", nw::EquipIndex::bolts)
        .value("creature_left", nw::EquipIndex::creature_left)
        .value("creature_right", nw::EquipIndex::creature_right)
        .value("creature_bite", nw::EquipIndex::creature_bite)
        .value("creature_skin", nw::EquipIndex::creature_skin)
        .value("invalid", nw::EquipIndex::invalid);

    py::class_<nw::Equips>(m, "Equips")
        .def_readonly("equips", &nw::Equips::equips);
}

void init_component_inventory(py::module& m)
{
    py::class_<nw::InventoryItem>(m, "InventoryItem")
        .def_readwrite("infinite", &nw::InventoryItem::infinite)
        .def_readwrite("x", &nw::InventoryItem::pos_x)
        .def_readwrite("y", &nw::InventoryItem::pos_y)
        .def_readwrite("item", &nw::InventoryItem::item);

    py::class_<nw::Inventory>(m, "Inventory")
        .def("instantiate", &nw::Inventory::instantiate)
        .def_readwrite("owner", &nw::Inventory::owner)
        .def_readonly("items", &nw::Inventory::items);
}

void init_component_levelstats(py::module& m)
{
    py::class_<nw::LevelStats>(m, "LevelStats");
}

void init_component_localdata(py::module& m)
{
    pybind11::class_<nw::LocalData>(m, "LocalData")
        .def(py::init<>())
        .def("delete_float", &nw::LocalData::delete_float)
        .def("delete_int", &nw::LocalData::delete_int)
        .def("delete_object", &nw::LocalData::delete_object)
        .def("delete_string", &nw::LocalData::delete_string)
        .def("delete_location", &nw::LocalData::delete_location)
        .def("get_float", &nw::LocalData::get_float)
        .def("get_int", &nw::LocalData::get_int)
        .def("get_object", &nw::LocalData::get_object)
        .def("get_string", &nw::LocalData::get_string)
        .def("get_location", &nw::LocalData::get_location)
        .def("set_float", &nw::LocalData::set_float)
        .def("set_int", &nw::LocalData::set_int)
        .def("set_object", &nw::LocalData::set_object)
        .def("set_string", &nw::LocalData::set_string)
        .def("set_location", &nw::LocalData::set_location)
        .def("size", &nw::LocalData::size);
}

void init_component_location(py::module& m)
{
    pybind11::class_<nw::Location>(m, "Location")
        .def(py::init<>())
        .def_readwrite("area", &nw::Location::area)
        .def_readwrite("position", &nw::Location::position)
        .def_readwrite("orientation", &nw::Location::orientation);
}

void init_component_lock(py::module& m)
{
    py::class_<nw::Lock>(m, "Lock")
        .def_readwrite("key_name", &nw::Lock::key_name)
        .def_readwrite("key_required", &nw::Lock::key_required)
        .def_readwrite("lockable", &nw::Lock::lockable)
        .def_readwrite("locked", &nw::Lock::locked)
        .def_readwrite("lock_dc", &nw::Lock::lock_dc)
        .def_readwrite("unlock_dc", &nw::Lock::unlock_dc)
        .def_readwrite("remove_key", &nw::Lock::remove_key);
}

void init_component_saves(py::module& m)
{
    py::class_<nw::Saves>(m, "Saves")
        .def_readwrite("fort", &nw::Saves::fort)
        .def_readwrite("reflex", &nw::Saves::reflex)
        .def_readwrite("will", &nw::Saves::will);
}

void init_component_spellbook(py::module& m)
{
    py::class_<nw::SpellBook>(m, "SpellBook");
}

void init_component_trap(py::module& m)
{
    py::class_<nw::Trap>(m, "Trap")
        .def(py::init<>())
        .def_readwrite("is_trapped", &nw::Trap::is_trapped)
        .def_readwrite("type", &nw::Trap::type)
        .def_readwrite("detectable", &nw::Trap::detectable)
        .def_readwrite("detect_dc", &nw::Trap::detect_dc)
        .def_readwrite("disarmable", &nw::Trap::disarmable)
        .def_readwrite("disarm_dc", &nw::Trap::disarm_dc)
        .def_readwrite("one_shot", &nw::Trap::one_shot);
}

void init_object_components(py::module& m)
{
    init_component_appearance(m);
    init_component_combatinfo(m);
    init_component_common(m);
    init_component_creature_stats(m);
    init_component_equips(m);
    init_component_inventory(m);
    init_component_levelstats(m);
    init_component_localdata(m);
    init_component_location(m);
    init_component_lock(m);
    init_component_saves(m);
    init_component_spellbook(m);
    init_component_trap(m);
}
