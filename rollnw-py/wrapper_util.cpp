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

    py::enum_<nw::PathAlias>(nw, "PathAlias")
        .value("ambient", nw::PathAlias::ambient)
        .value("cache", nw::PathAlias::cache)
        .value("currentgame", nw::PathAlias::currentgame)
        .value("database", nw::PathAlias::database)
        .value("development", nw::PathAlias::development)
        .value("dmvault", nw::PathAlias::dmvault)
        .value("hak", nw::PathAlias::hak)
        .value("hd0", nw::PathAlias::hd0)
        .value("localvault", nw::PathAlias::localvault)
        .value("logs", nw::PathAlias::logs)
        .value("modelcompiler", nw::PathAlias::modelcompiler)
        .value("modules", nw::PathAlias::modules)
        .value("movies", nw::PathAlias::movies)
        .value("music", nw::PathAlias::music)
        .value("nwsync", nw::PathAlias::nwsync)
        .value("oldservervault", nw::PathAlias::oldservervault)
        .value("override", nw::PathAlias::override)
        .value("patch", nw::PathAlias::patch)
        .value("portraits", nw::PathAlias::portraits)
        .value("saves", nw::PathAlias::saves)
        .value("screenshots", nw::PathAlias::screenshots)
        .value("servervault", nw::PathAlias::servervault)
        .value("temp", nw::PathAlias::temp)
        .value("tempclient", nw::PathAlias::tempclient)
        .value("tlk", nw::PathAlias::tlk)
        .value("user", nw::PathAlias::user);

    nw.def("probe_nwn_install", &nw::probe_nwn_install, py::arg("only") = nw::GameVersion::invalid);
}
