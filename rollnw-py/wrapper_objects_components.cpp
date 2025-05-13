#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/objects/Appearance.hpp>
#include <nw/objects/CombatInfo.hpp>
#include <nw/objects/Common.hpp>
#include <nw/objects/CreatureStats.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/Inventory.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/LevelHistory.hpp>
#include <nw/objects/LevelStats.hpp>
#include <nw/objects/Location.hpp>
#include <nw/objects/Lock.hpp>
#include <nw/objects/Saves.hpp>
#include <nw/objects/SpellBook.hpp>
#include <nw/objects/Trap.hpp>

#include "json/json.hpp"
#include <nanobind/nanobind.h>

namespace py = nanobind;

void init_component_appearance(py::module_& m)
{
    py::enum_<nw::CreatureColors::type>(m, "CreatureColors")
        .value("hair", nw::CreatureColors::hair)
        .value("skin", nw::CreatureColors::skin)
        .value("tatoo1", nw::CreatureColors::tatoo1)
        .value("tatoo2", nw::CreatureColors::tatoo2);

    py::class_<nw::BodyParts>(m, "BodyParts")
        .def(py::init<>())
        .def_rw("belt", &nw::BodyParts::belt)
        .def_rw("bicep_left", &nw::BodyParts::bicep_left)
        .def_rw("bicep_right", &nw::BodyParts::bicep_right)
        .def_rw("foot_left", &nw::BodyParts::foot_left)
        .def_rw("foot_right", &nw::BodyParts::foot_right)
        .def_rw("forearm_left", &nw::BodyParts::forearm_left)
        .def_rw("forearm_right", &nw::BodyParts::forearm_right)
        .def_rw("hand_left", &nw::BodyParts::hand_left)
        .def_rw("hand_right", &nw::BodyParts::hand_right)
        .def_rw("head", &nw::BodyParts::head)
        .def_rw("neck", &nw::BodyParts::neck)
        .def_rw("pelvis", &nw::BodyParts::pelvis)
        .def_rw("shin_left", &nw::BodyParts::shin_left)
        .def_rw("shin_right", &nw::BodyParts::shin_right)
        .def_rw("shoulder_left", &nw::BodyParts::shoulder_left)
        .def_rw("shoulder_right", &nw::BodyParts::shoulder_right)
        .def_rw("thigh_left", &nw::BodyParts::thigh_left)
        .def_rw("thigh_right", &nw::BodyParts::thigh_right);

    py::class_<nw::CreatureAppearance>(m, "Appearance")
        .def(py::init<>())
        .def_rw("phenotype", &nw::CreatureAppearance::phenotype)
        .def_rw("tail", &nw::CreatureAppearance::tail)
        .def_rw("wings", &nw::CreatureAppearance::wings)
        .def_rw("id", &nw::CreatureAppearance::id)
        .def_rw("portrait_id", &nw::CreatureAppearance::portrait_id)
        .def_rw("body_parts", &nw::CreatureAppearance::body_parts)
        .def_rw("colors", &nw::CreatureAppearance::colors);
}

void init_component_combatinfo(py::module_& m)
{
    py::class_<nw::CombatInfo>(m, "CombatInfo")
        .def_rw("ac_natural_bonus", &nw::CombatInfo::ac_natural_bonus)
        .def_rw("ac_armor_base", &nw::CombatInfo::ac_armor_base)
        .def_rw("ac_shield_base", &nw::CombatInfo::ac_shield_base)
        .def_rw("combat_mode", &nw::CombatInfo::combat_mode)
        .def_rw("target_state", &nw::CombatInfo::target_state)
        .def_rw("size_ab_modifier", &nw::CombatInfo::size_ab_modifier)
        .def_rw("size_ac_modifier", &nw::CombatInfo::size_ac_modifier);
}

void init_component_common(py::module_& m)
{
    py::class_<nw::Common>(m, "Common")
        .def_rw("resref", &nw::Common::resref)
        .def_rw("tag", &nw::Common::tag)
        .def_rw("name", &nw::Common::name)
        .def_rw("locals", &nw::Common::locals)
        .def_rw("location", &nw::Common::location)
        .def_rw("comment", &nw::Common::comment)
        .def_rw("palette_id", &nw::Common::palette_id);
}

void init_component_creature_stats(py::module_& m)
{
    py::class_<nw::CreatureStats>(m, "CreatureStats")
        .def("add_feat", &nw::CreatureStats::add_feat)
        .def("get_ability_score", &nw::CreatureStats::get_ability_score)
        .def("get_skill_rank", &nw::CreatureStats::get_skill_rank)
        .def("has_feat", &nw::CreatureStats::has_feat)
        .def("remove_feat", &nw::CreatureStats::remove_feat)
        .def("set_ability_score", &nw::CreatureStats::set_ability_score)
        .def("set_skill_rank", &nw::CreatureStats::set_skill_rank)
        .def_ro("save_bonus", &nw::CreatureStats::save_bonus);
}

void init_component_equips(py::module_& m)
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
        .def_ro("equips", &nw::Equips::equips)
        .def("instantiate", &nw::Equips::instantiate);
}

void init_component_inventory(py::module_& m)
{
    py::class_<nw::InventoryItem>(m, "InventoryItem")
        .def_rw("infinite", &nw::InventoryItem::infinite)
        .def_rw("x", &nw::InventoryItem::pos_x)
        .def_rw("y", &nw::InventoryItem::pos_y)
        .def_rw("item", &nw::InventoryItem::item);

    py::class_<nw::Inventory>(m, "Inventory")
        .def("__len__", &nw::Inventory::size)
        .def("instantiate", &nw::Inventory::instantiate)

        .def("add_item", &nw::Inventory::add_item, py::keep_alive<1, 2>())
        .def("can_add_item", &nw::Inventory::can_add_item)
        .def("debug", &nw::Inventory::debug)
        .def("has_item", &nw::Inventory::has_item)
        .def("remove_item", &nw::Inventory::remove_item)

        .def_rw("owner", &nw::Inventory::owner)
        .def("items", [](const nw::Inventory& self) {
            auto pylist = py::list();
            for (auto& item : self.items) {
                if (item.item.is<nw::Item*>()) {
                    auto pyobj = py::cast(item.item.as<nw::Item*>(), py::rv_policy::reference);
                    pylist.append(pyobj);
                }
            }
            return pylist;
        })
        .def("remove_item", &nw::Inventory::remove_item)
        .def("size", &nw::Inventory::size);
}

void init_component_levelhistory(py::module_& m)
{
    py::class_<nw::LevelUp>(m, "LevelUp")
        .def_rw("class_", &nw::LevelUp::class_)
        .def_rw("ability", &nw::LevelUp::ability)
        .def_rw("epic", &nw::LevelUp::epic)
        .def_rw("feats", &nw::LevelUp::feats)
        .def_rw("hitpoints", &nw::LevelUp::hitpoints)
        .def_rw("known_spells", &nw::LevelUp::known_spells)
        .def_rw("skillpoints", &nw::LevelUp::skillpoints)
        .def_rw("skills", &nw::LevelUp::skills);

    py::class_<nw::LevelHistory>(m, "LevelHistory")
        .def_rw("entries", &nw::LevelHistory::entries);
}

void init_component_levelstats(py::module_& m)
{
    py::class_<nw::ClassEntry>(m, "ClassEntry")
        .def_rw("id", &nw::ClassEntry::id)
        .def_rw("level", &nw::ClassEntry::level)
        .def_rw("spells", &nw::ClassEntry::spells);

    py::class_<nw::LevelStats>(m, "LevelStats")
        .def("level", &nw::LevelStats::level)
        .def("level_by_class", &nw::LevelStats::level_by_class)
        .def_ro("entries", &nw::LevelStats::entries);
}

void init_component_localdata(py::module_& m)
{
    py::class_<nw::LocalData>(m, "LocalData")
        .def(py::init<>())
        .def("clear", &nw::LocalData::clear)
        .def("clear_all", &nw::LocalData::clear_all)
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

void init_component_location(py::module_& m)
{
    py::class_<nw::Location>(m, "Location")
        .def(py::init<>())
        .def_rw("area", &nw::Location::area)
        .def_rw("position", &nw::Location::position)
        .def_rw("orientation", &nw::Location::orientation);
}

void init_component_lock(py::module_& m)
{
    py::class_<nw::Lock>(m, "Lock")
        .def_rw("key_name", &nw::Lock::key_name)
        .def_rw("key_required", &nw::Lock::key_required)
        .def_rw("lockable", &nw::Lock::lockable)
        .def_rw("locked", &nw::Lock::locked)
        .def_rw("lock_dc", &nw::Lock::lock_dc)
        .def_rw("unlock_dc", &nw::Lock::unlock_dc)
        .def_rw("remove_key", &nw::Lock::remove_key);
}

void init_component_saves(py::module_& m)
{
    py::class_<nw::Saves>(m, "Saves")
        .def_rw("fort", &nw::Saves::fort)
        .def_rw("reflex", &nw::Saves::reflex)
        .def_rw("will", &nw::Saves::will);
}

void init_component_spellbook(py::module_& m)
{
    py::class_<nw::SpecialAbility>(m, "SpecialAbility")
        .def_rw("spell", &nw::SpecialAbility::spell)
        .def_rw("level", &nw::SpecialAbility::level)
        .def_rw("flags", &nw::SpecialAbility::flags);

    py::enum_<nw::SpellFlags>(m, "SpellFlags")
        .value("none", nw::SpellFlags::none)
        .value("readied", nw::SpellFlags::readied)
        .value("spontaneous", nw::SpellFlags::spontaneous)
        .value("unlimited", nw::SpellFlags::unlimited);

    py::class_<nw::SpellEntry>(m, "SpellEntry")
        .def_rw("spell", &nw::SpellEntry::spell)
        .def_rw("meta", &nw::SpellEntry::meta)
        .def_rw("flags", &nw::SpellEntry::flags);

    py::class_<nw::SpellBook>(m, "SpellBook")
        .def("add_known_spell", &nw::SpellBook::add_known_spell)
        .def("add_memorized_spell", &nw::SpellBook::add_memorized_spell)
        .def("clear_memorized_spell", &nw::SpellBook::clear_memorized_spell)
        .def("get_known_spell_count", &nw::SpellBook::get_known_spell_count)
        .def("get_memorized_spell_count", &nw::SpellBook::get_memorized_spell_count)
        .def("get_known_spell", &nw::SpellBook::get_known_spell)
        .def("get_memorized_spell", &nw::SpellBook::get_memorized_spell)
        .def("remove_known_spell", &nw::SpellBook::remove_known_spell);
}

void init_component_trap(py::module_& m)
{
    py::class_<nw::Trap>(m, "Trap")
        .def(py::init<>())
        .def_rw("is_trapped", &nw::Trap::is_trapped)
        .def_rw("type", &nw::Trap::type)
        .def_rw("detectable", &nw::Trap::detectable)
        .def_rw("detect_dc", &nw::Trap::detect_dc)
        .def_rw("disarmable", &nw::Trap::disarmable)
        .def_rw("disarm_dc", &nw::Trap::disarm_dc)
        .def_rw("one_shot", &nw::Trap::one_shot);
}

void init_object_components(py::module_& m)
{
    init_component_appearance(m);
    init_component_combatinfo(m);
    init_component_common(m);
    init_component_creature_stats(m);
    init_component_equips(m);
    init_component_inventory(m);
    init_component_levelhistory(m);
    init_component_levelstats(m);
    init_component_localdata(m);
    init_component_location(m);
    init_component_lock(m);
    init_component_saves(m);
    init_component_spellbook(m);
    init_component_trap(m);
}
