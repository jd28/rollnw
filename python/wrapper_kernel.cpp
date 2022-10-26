#include <nw/components/Area.hpp>
#include <nw/components/Creature.hpp>
#include <nw/components/Door.hpp>
#include <nw/components/Module.hpp>
#include <nw/components/Trigger.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nwn1/Profile.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <filesystem>

namespace py = pybind11;
namespace fs = std::filesystem;

template <typename T>
T* load_object_helper(std::string_view resref)
{
    return nw::kernel::objects().load<T>(resref);
}

template <typename T>
T* load_object_helper_fs(const fs::path& path, nw::SerializationProfile profile)
{
    return nw::kernel::objects().load<T>(path, profile);
}

void init_kernel_config(py::module& nw, py::module& kernel)
{
    py::class_<nw::ConfigOptions>(nw, "ConfigOptions")
        .def(py::init<>())
        .def_readwrite("version", &nw::ConfigOptions::version)
        .def_readwrite("install", &nw::ConfigOptions::install)
        .def_readwrite("user", &nw::ConfigOptions::user)
        .def_readwrite("include_install", &nw::ConfigOptions::include_install)
        .def_readwrite("include_nwsync", &nw::ConfigOptions::include_nwsync);

    kernel.def("config_initialize", [](const nw::ConfigOptions& options) {
        nw::kernel::config().initialize(options);
    });
}

void init_kernel_objects(py::module& nw, py::module& kernel)
{
}

void init_kernel(py::module& nw, py::module& kernel)
{
    init_kernel_config(nw, kernel);

    kernel.def("load_module", &nw::kernel::load_module, py::return_value_policy::reference)
        .def("unload_module", &nw::kernel::unload_module);

    kernel.def("creature", &load_object_helper<nw::Creature>, py::return_value_policy::reference)
        .def("creature", &load_object_helper_fs<nw::Creature>, py::return_value_policy::reference)
        .def("door", &load_object_helper<nw::Door>, py::return_value_policy::reference)
        .def("door", &load_object_helper_fs<nw::Door>, py::return_value_policy::reference)

        ;

    kernel.def("start", []() {
        auto info = nw::probe_nwn_install();
        nw::kernel::config().initialize({
            info.version,
            info.install,
            info.user,
        });
        nw::kernel::services().start();
        nw::kernel::load_profile(new nwn1::Profile);
    });
}
