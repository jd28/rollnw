#include "casters.hpp"

#include <nw/objects/Creature.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/combat.hpp>

#include <fmt/format.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <string>

namespace py = pybind11;

void init_rules(py::module& nw)
{
    py::enum_<nw::AttackResult>(nw, "AttackResult")
        .value("hit_by_auto_success", nw::AttackResult::hit_by_auto_success)
        .value("hit_by_critical", nw::AttackResult::hit_by_critical)
        .value("hit_by_roll", nw::AttackResult::hit_by_roll)
        .value("miss_by_auto_fail", nw::AttackResult::miss_by_auto_fail)
        .value("miss_by_concealment", nw::AttackResult::miss_by_concealment)
        .value("miss_by_miss_chance", nw::AttackResult::miss_by_miss_chance)
        .value("miss_by_roll", nw::AttackResult::miss_by_roll);

    py::class_<nw::AttackData>(nw, "AttackData")
        .def_readwrite("attacker", &nw::AttackData::attacker)
        .def_readwrite("target", &nw::AttackData::target)
        .def_readwrite("type", &nw::AttackData::type)
        .def_readwrite("result", &nw::AttackData::result)
        .def_readwrite("target_state", &nw::AttackData::target_state)
        .def_readwrite("target_is_creature", &nw::AttackData::target_is_creature)
        .def_readwrite("is_ranged_attack", &nw::AttackData::is_ranged_attack)
        .def_readwrite("nth_attack", &nw::AttackData::nth_attack)
        .def_readwrite("attack_roll", &nw::AttackData::attack_roll)
        .def_readwrite("attack_bonus", &nw::AttackData::attack_bonus)
        .def_readwrite("armor_class", &nw::AttackData::armor_class)
        .def_readwrite("iteration_penalty", &nw::AttackData::iteration_penalty)
        .def_readwrite("multiplier", &nw::AttackData::multiplier)
        .def_readwrite("threat_range", &nw::AttackData::threat_range)
        .def_readwrite("concealment", &nw::AttackData::concealment);

    py::class_<nw::DiceRoll>(nw, "DiceRoll")
        .def_readwrite("dice", &nw::DiceRoll::dice)
        .def_readwrite("sides", &nw::DiceRoll::sides)
        .def_readwrite("bonus", &nw::DiceRoll::bonus);

    py::enum_<nw::EffectCategory>(nw, "EffectCategory")
        .value("magical", nw::EffectCategory::magical)
        .value("extraordinary", nw::EffectCategory::extraordinary)
        .value("supernatural", nw::EffectCategory::supernatural)
        .value("item", nw::EffectCategory::item)
        .value("innate", nw::EffectCategory::innate);

    py::class_<nw::EffectID>(nw, "EffectID")
        .def_readonly("version", &nw::EffectID::version)
        .def_readonly("index", &nw::EffectID::index);

    py::class_<nw::EffectHandle>(nw, "EffectHandle")
        .def_readonly("type", &nw::EffectHandle::type)
        .def_readonly("subtype", &nw::EffectHandle::subtype)
        .def_readonly("creator", &nw::EffectHandle::creator)
        .def_readonly("spell_id", &nw::EffectHandle::spell_id)
        .def_readonly("category", &nw::EffectHandle::category)
        .def_readonly("effect", &nw::EffectHandle::effect);

    py::class_<nw::Effect>(nw, "Effect")
        .def("clear", &nw::Effect::clear)
        .def("get_float", &nw::Effect::get_float)
        .def("get_int", &nw::Effect::get_int)
        .def("get_string", &nw::Effect::get_string)
        .def("handle", &nw::Effect::handle)
        .def("id", &nw::Effect::id)
        .def("set_float", &nw::Effect::set_float)
        //.def("set_id", &nw::Effect::set_id)
        .def("set_int", &nw::Effect::set_int)
        .def("set_string", &nw::Effect::set_string)
        .def("set_versus", &nw::Effect::set_versus)
        .def("versus", &nw::Effect::versus)
        .def_readwrite("type", &nw::Effect::type)
        .def_readwrite("category", &nw::Effect::category)
        .def_readwrite("subtype", &nw::Effect::subtype)
        .def_readwrite("creator", &nw::Effect::creator)
        .def_readwrite("spell_id", &nw::Effect::spell_id)
        .def_readwrite("duration", &nw::Effect::duration)
        .def_readwrite("expire_day", &nw::Effect::expire_day)
        .def_readwrite("expire_time", &nw::Effect::expire_time);
}
