#include "glm/wrap_vmath.h"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/stl_bind.h>

#include <nw/util/platform.hpp>

namespace py = pybind11;

void init_i18n(py::module& nw);
void init_formats(py::module& nw);
void init_objects(py::module& nw);
void init_resources(py::module& nw);
void init_rules(py::module& nw);
void init_serialization(py::module& nw);
void init_util(py::module& nw);

void init_kernel(py::module& kernel);
void init_model(py::module& nw);
void init_script(py::module& nw);

void init_nwn1(py::module& m);

PYBIND11_MODULE(rollnw, nw)
{
    nw.doc() = "rollnw python wrapper";

    bind_opaque_types(nw);
    // Initialize submodules
    init_i18n(nw);
    init_formats(nw);
    init_objects(nw);
    init_resources(nw);
    init_rules(nw);
    init_serialization(nw);
    init_util(nw);
    wrap_vmath(nw);

    py::module kernel = nw.def_submodule("kernel");
    init_kernel(kernel);
    py::module script = nw.def_submodule("script");
    init_script(script);
    py::module model = nw.def_submodule("model");
    init_model(model);
    py::module nwn1 = nw.def_submodule("nwn1");
    init_nwn1(nwn1);
}
