#include <nw/util/ByteArray.hpp>
#include <nw/util/game_install.hpp>

#include <nanobind/nanobind.h>

namespace py = nanobind;

void init_util(py::module_& nw)
{
    py::enum_<nw::GameVersion>(nw, "GameVersion")
        .value("invalid", nw::GameVersion::invalid)
        .value("v1_69", nw::GameVersion::v1_69)
        .value("vEE", nw::GameVersion::vEE);

    py::class_<nw::InstallInfo>(nw, "InstallInfo")
        .def_rw("install", &nw::InstallInfo::install)
        .def_rw("user", &nw::InstallInfo::user)
        .def_rw("version", &nw::InstallInfo::version);

    nw.def("probe_nwn_install", &nw::probe_nwn_install, py::arg("only") = nw::GameVersion::invalid);
}
