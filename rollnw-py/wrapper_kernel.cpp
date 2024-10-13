#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/stl_bind.h>

#include <filesystem>
#include <initializer_list>

namespace py = pybind11;
namespace fs = std::filesystem;

void init_kernel_config(py::module& kernel)
{
    py::class_<nw::ConfigOptions>(kernel, "ConfigOptions")
        .def_readwrite("include_install", &nw::ConfigOptions::include_install)
        .def_readwrite("include_nwsync", &nw::ConfigOptions::include_nwsync)
        .def_readwrite("include_user", &nw::ConfigOptions::include_user);

    py::class_<nw::kernel::Config>(kernel, "Config")
        .def("initialize", [](const nw::ConfigOptions& options) {
            nw::kernel::config().initialize(options);
        })
        .def("install_path", &nw::kernel::Config::install_path)
        .def("options", &nw::kernel::Config::options)
        .def("user_path", &nw::kernel::Config::user_path)
        .def("set_paths", &nw::kernel::Config::set_paths)
        .def("set_version", &nw::kernel::Config::set_version)
        .def("version", &nw::kernel::Config::version);

    kernel.def(
        "config", []() {
            return &nw::kernel::config();
        },
        py::return_value_policy::reference);
}

void init_kernel_effects(py::module& kernel)
{
    py::class_<nw::kernel::EffectSystemStats>(kernel, "EffectSystemStats")
        .def_readonly("free_list_size", &nw::kernel::EffectSystemStats::free_list_size)
        .def_readonly("pool_size", &nw::kernel::EffectSystemStats::pool_size);

    py::class_<nw::kernel::EffectSystem>(kernel, "EffectSystem")
        // Unexposed:
        // - Effect* generate(const ItemProperty& property, EquipIndex index, BaseItem baseitem) const;
        // - virtual void initialize() override;
        // - virtual void clear() override;
        .def("add_effect", py::overload_cast<nw::EffectType, nw::kernel::EffectFunc, nw::kernel::EffectFunc>(&nw::kernel::EffectSystem::add))
        .def("add_itemprop", py::overload_cast<nw::ItemPropertyType, nw::kernel::ItemPropFunc>(&nw::kernel::EffectSystem::add))
        .def("apply", &nw::kernel::EffectSystem::apply)
        .def("create", &nw::kernel::EffectSystem::create, py::return_value_policy::reference)
        .def("destroy", &nw::kernel::EffectSystem::destroy)
        .def("ip_cost_table", &nw::kernel::EffectSystem::ip_cost_table)
        .def("ip_definition", &nw::kernel::EffectSystem::ip_definition)
        .def("ip_param_table", &nw::kernel::EffectSystem::ip_param_table)
        .def("remove", &nw::kernel::EffectSystem::remove)
        .def("stats", &nw::kernel::EffectSystem::stats);

    kernel.def(
        "effects", []() {
            return &nw::kernel::effects();
        },
        py::return_value_policy::reference);
}

template <typename T>
T* load_object_helper(nw::kernel::ObjectSystem& self, std::string_view resref)
{
    return self.load<T>(resref);
}

template <typename T>
T* load_object_helper_fs(nw::kernel::ObjectSystem& self, const fs::path& path, nw::SerializationProfile profile)
{
    return self.load<T>(path, profile);
}

void init_kernel_objects(py::module& kernel)
{
    py::class_<nw::kernel::ObjectSystem>(kernel, "Objects")
        .def("area", &nw::kernel::ObjectSystem::make_area,
            py::return_value_policy::reference_internal)
        .def("creature", &load_object_helper<nw::Creature>,
            py::return_value_policy::reference_internal)
        .def("destroy", &nw::kernel::ObjectSystem::destroy)
        .def("door", &load_object_helper<nw::Door>,
            py::return_value_policy::reference_internal)
        .def("encounter", &load_object_helper<nw::Encounter>,
            py::return_value_policy::reference_internal)
        .def("get", &nw::kernel::ObjectSystem::get_object_base,
            py::return_value_policy::reference_internal)
        .def("get_by_tag", &nw::kernel::ObjectSystem::get_by_tag,
            py::arg("tag"), py::arg("nth") = 0,
            py::return_value_policy::reference_internal)
        .def("item", &load_object_helper<nw::Item>,
            py::return_value_policy::reference_internal)
        .def("placeable", &load_object_helper<nw::Placeable>,
            py::return_value_policy::reference_internal)
        .def("store", &load_object_helper<nw::Store>,
            py::return_value_policy::reference_internal)
        .def("trigger", &load_object_helper<nw::Trigger>,
            py::return_value_policy::reference_internal)
        .def("valid", &nw::kernel::ObjectSystem::valid)
        .def("waypoint", &load_object_helper<nw::Waypoint>,
            py::return_value_policy::reference_internal);

    kernel.def(
        "objects", []() {
            return &nw::kernel::objects();
        },
        py::return_value_policy::reference);
}

void init_kernel_resources(py::module& kernel)
{
    py::class_<nw::kernel::Resources, nw::Container>(kernel, "Resources")
        .def("demand_any", &nw::kernel::Resources::demand_any)
        .def("demand_in_order", &nw::kernel::Resources::demand_in_order)
        .def("demand_server_vault", &nw::kernel::Resources::demand_server_vault)
        .def("texture", &nw::kernel::Resources::texture);

    kernel.def(
        "resman", []() {
            return &nw::kernel::resman();
        },
        py::return_value_policy::reference);
}

void init_kernel_rules(py::module& kernel)
{
    py::class_<nw::kernel::Rules>(kernel, "Rules");
    kernel.def(
        "rules", []() {
            return &nw::kernel::rules();
        },
        py::return_value_policy::reference);
}

void init_kernel_strings(py::module& kernel)
{
    py::class_<nw::kernel::Strings>(kernel, "Strings");
    kernel.def(
        "strings", []() {
            return &nw::kernel::strings();
        },
        py::return_value_policy::reference);
}

void init_kernel_twoda_cache(py::module& kernel)
{
    py::class_<nw::kernel::TwoDACache>(kernel, "TwoDACache")
        .def("get", py::overload_cast<std::string_view>(&nw::kernel::TwoDACache::get),
            py::return_value_policy::reference_internal)
        .def("get", py::overload_cast<const nw::Resource&>(&nw::kernel::TwoDACache::get),
            py::return_value_policy::reference_internal);

    kernel.def(
        "twodas", []() {
            return &nw::kernel::twodas();
        },
        py::return_value_policy::reference);
}

void init_kernel(py::module& kernel)
{
    init_kernel_config(kernel);
    init_kernel_effects(kernel);
    init_kernel_objects(kernel);
    init_kernel_resources(kernel);
    init_kernel_rules(kernel);
    init_kernel_strings(kernel);
    init_kernel_twoda_cache(kernel);

    kernel.def("load_module", [](const std::string& path, bool instantiate = true) { return nw::kernel::load_module(path, instantiate); }, py::arg("path"), py::arg("instantiate") = true, py::return_value_policy::reference_internal);
    kernel.def("unload_module", &nw::kernel::unload_module);

    kernel.def("start", []() {
              nw::kernel::config().initialize();
              nw::kernel::services().start();
          })
        .def("start", [](const nw::ConfigOptions& config) {
            nw::kernel::config().initialize(config);
            nw::kernel::services().start();
        });
}
