#include <nw/util/ByteArray.hpp>
#include <nw/util/game_install.hpp>

#include <pybind11/pybind11.h>

namespace py = pybind11;

void init_util(pybind11::module& nw)
{
    py::enum_<nw::GameVersion>(nw, "GameVersion")
        .value("invalid", nw::GameVersion::invalid)
        .value("v1_69", nw::GameVersion::v1_69)
        .value("vEE", nw::GameVersion::vEE);

    py::class_<nw::InstallInfo>(nw, "InstallInfo")
        .def_readwrite("install", &nw::InstallInfo::install)
        .def_readwrite("user", &nw::InstallInfo::user)
        .def_readwrite("version", &nw::InstallInfo::version);

    nw.def("probe_nwn_install", &nw::probe_nwn_install, py::arg("only") = nw::GameVersion::invalid);
}
