#include "casters.hpp"

#include <nw/resources/Container.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/resources/StaticDirectory.hpp>
#include <nw/resources/StaticKey.hpp>
#include <nw/resources/StaticZip.hpp>
#include <nw/resources/assets.hpp>
#include <nw/util/string.hpp>

#include <fmt/format.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <string>

namespace py = pybind11;

void init_resources_data(py::module& nw)
{
    py::class_<nw::ResourceData>(nw, "ResourceData")
        .def_readwrite("name", &nw::ResourceData::name)
        .def_readwrite("bytes", &nw::ResourceData::bytes);
}

void init_resources_container(py::module& nw)
{
    py::class_<nw::Container>(nw, "Container")
        .def("name", &nw::Container::name)
        .def("path", &nw::Container::path)
        .def("size", &nw::Container::size)
        .def("valid", &nw::Container::valid);
}

void init_resources_erf(py::module& nw)
{
    py::class_<nw::Erf>(nw, "Erf")
        .def(py::init<>())
        .def(py::init<std::filesystem::path>())
        //.def("add", static_cast<bool (nw::Erf::*)(nw::Resource res, const nw::ByteArray&)>(&nw::Erf::add))
        .def("add", static_cast<bool (nw::Erf::*)(const std::filesystem::path&)>(&nw::Erf::add))
        .def("erase", &nw::Erf::erase)
        .def("merge", static_cast<bool (nw::Erf::*)(const nw::Erf&)>(&nw::Erf::merge))
        .def("reload", &nw::Erf::reload)
        .def("save", &nw::Erf::save)
        .def("save_as", &nw::Erf::save_as)

        .def_readwrite("description", &nw::Erf::description)

        .def("demand", [](const nw::Erf& self, std::string_view name) {
            return self.demand(nw::Resource::from_filename(name));
        })
        .def("name", &nw::Erf::name)
        .def("path", &nw::Erf::path)
        .def("size", &nw::Erf::size)
        .def("valid", &nw::Erf::valid);
}

void init_resource_manager(py::module& kernel)
{
    py::class_<nw::ResourceManager>(kernel, "ResourceManager")
        .def("demand", [](const nw::ResourceManager& self, std::string_view name) {
            return self.demand(nw::Resource::from_filename(name));
        })
        .def("demand_server_vault", &nw::ResourceManager::demand_server_vault)
        .def("texture", &nw::ResourceManager::texture);
}

void init_resources(py::module& nw)
{
    init_resources_data(nw);
    init_resources_container(nw);
    init_resources_erf(nw);
}
