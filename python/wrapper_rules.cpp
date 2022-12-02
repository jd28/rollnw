#include "casters.hpp"

#include <nw/rules/Effect.hpp>

#include <fmt/format.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <string>

namespace py = pybind11;

void init_rules(py::module& nw)
{
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
