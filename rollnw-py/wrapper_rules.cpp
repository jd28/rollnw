#include "casters.hpp"

#include <nw/objects/Creature.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/combat.hpp>

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/operators.h>
#include <nanobind/stl/filesystem.h>

#include <string>

namespace py = nanobind;

void init_rules(py::module_& nw)
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
        .def_rw("attacker", &nw::AttackData::attacker)
        .def_rw("target", &nw::AttackData::target)
        .def_rw("type", &nw::AttackData::type)
        .def_rw("result", &nw::AttackData::result)
        .def_rw("target_state", &nw::AttackData::target_state)
        .def_rw("target_is_creature", &nw::AttackData::target_is_creature)
        .def_rw("is_ranged_attack", &nw::AttackData::is_ranged_attack)
        .def_rw("nth_attack", &nw::AttackData::nth_attack)
        .def_rw("attack_roll", &nw::AttackData::attack_roll)
        .def_rw("attack_bonus", &nw::AttackData::attack_bonus)
        .def_rw("armor_class", &nw::AttackData::armor_class)
        .def_rw("iteration_penalty", &nw::AttackData::iteration_penalty)
        .def_rw("multiplier", &nw::AttackData::multiplier)
        .def_rw("threat_range", &nw::AttackData::threat_range)
        .def_rw("concealment", &nw::AttackData::concealment);

    py::enum_<nw::DamageCategory>(nw, "DamageCategory")
        .value("none", nw::DamageCategory::none, "No special damage category.")
        .value("penalty", nw::DamageCategory::penalty, "Penalty damage.")
        .value("critical", nw::DamageCategory::critical, "Critical damage.")
        .value("unblockable", nw::DamageCategory::unblockable, "Unblockable damage.")
        .export_values();

    py::class_<nw::DiceRoll>(nw, "DiceRoll")
        .def_rw("dice", &nw::DiceRoll::dice)
        .def_rw("sides", &nw::DiceRoll::sides)
        .def_rw("bonus", &nw::DiceRoll::bonus);

    py::enum_<nw::EffectCategory>(nw, "EffectCategory")
        .value("magical", nw::EffectCategory::magical)
        .value("extraordinary", nw::EffectCategory::extraordinary)
        .value("supernatural", nw::EffectCategory::supernatural)
        .value("item", nw::EffectCategory::item)
        .value("innate", nw::EffectCategory::innate);

    py::class_<nw::EffectID>(nw, "EffectID")
        .def_ro("version", &nw::EffectID::version)
        .def_ro("index", &nw::EffectID::index);

    py::class_<nw::EffectHandle>(nw, "EffectHandle")
        .def_ro("type", &nw::EffectHandle::type)
        .def_ro("subtype", &nw::EffectHandle::subtype)
        .def_ro("creator", &nw::EffectHandle::creator)
        .def_ro("spell_id", &nw::EffectHandle::spell_id)
        .def_ro("category", &nw::EffectHandle::category)
        .def_ro("effect", &nw::EffectHandle::effect);

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
        .def_rw("type", &nw::Effect::type)
        .def_rw("category", &nw::Effect::category)
        .def_rw("subtype", &nw::Effect::subtype)
        .def_rw("creator", &nw::Effect::creator)
        .def_rw("spell_id", &nw::Effect::spell_id)
        .def_rw("duration", &nw::Effect::duration)
        .def_rw("expire_day", &nw::Effect::expire_day)
        .def_rw("expire_time", &nw::Effect::expire_time);
}
