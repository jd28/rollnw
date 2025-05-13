#include "glm/wrap_vmath.h"
#include "opaque_types.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>

#include <nw/util/platform.hpp>

namespace py = nanobind;

void init_i18n(py::module_& nw);
void init_formats(py::module_& nw);
void init_objects(py::module_& nw);
void init_resources(py::module_& nw);
void init_rules(py::module_& nw);
void init_serialization(py::module_& nw);
void init_util(py::module_& nw);
void init_kernel(py::module_& kernel);
void init_model(py::module_& nw);
void init_script(py::module_& nw);

void init_core_scriptapi(py::module_& m);
void init_nwn1(py::module_& m);

NB_MODULE(rollnw, nw)
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

    py::module_ kernel = nw.def_submodule("kernel");
    init_kernel(kernel);
    py::module_ script = nw.def_submodule("script");
    init_script(script);
    py::module_ model = nw.def_submodule("model");
    init_model(model);

    // Profiles - NWN1
    py::module_ nwn1 = nw.def_submodule("nwn1");
    init_core_scriptapi(nwn1);
    init_nwn1(nwn1);
}
