#include "casters.hpp"

#include "nw/objects/Creature.hpp"
#include "nw/objects/Item.hpp"
#include "nw/objects/ObjectBase.hpp"
#include "nw/rules/Effect.hpp"
#include "nw/rules/items.hpp"
#include "nw/scriptapi.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

void init_core_scriptapi(py::module& m)
{
    m.def("create_object", &nw::create_object);
    m.def("destroy_object", &nw::destroy_object);

    m.def("apply_effect", &nw::apply_effect, py::arg("obj"), py::arg("effect"));
    m.def("has_effect_applied", &nw::has_effect_applied, py::arg("obj"), py::arg("type"), py::arg("subtype") = -1);
    m.def("remove_effect", &nw::remove_effect, py::arg("obj"), py::arg("effect"), py::arg("destroy") = true);
    m.def("remove_effects_by", &nw::remove_effects_by, py::arg("obj"), py::arg("creator"));

    m.def("count_feats_in_range", &nw::count_feats_in_range, py::arg("obj"), py::arg("start"), py::arg("end"));
    m.def("get_all_available_feats", &nw::get_all_available_feats, py::arg("obj"));
    m.def("has_feat_successor", &nw::has_feat_successor, py::arg("obj"), py::arg("feat"));
    m.def("highest_feat_in_range", &nw::highest_feat_in_range, py::arg("obj"), py::arg("start"), py::arg("end"));
    m.def("knows_feat", &nw::knows_feat, py::arg("obj"), py::arg("feat"));

    m.def("item_has_property", &nw::item_has_property, py::arg("item"), py::arg("type"), py::arg("subtype") = -1);
    m.def("itemprop_to_string", &nw::itemprop_to_string, py::arg("ip"));
}
