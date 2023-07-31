#include "casters.hpp"

#include <nw/resources/Container.hpp>
#include <nw/resources/Directory.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/Key.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/Resource.hpp>
#include <nw/resources/ResourceType.hpp>
#include <nw/resources/Resref.hpp>
#include <nw/resources/Zip.hpp>
#include <nw/util/string.hpp>

#include <fmt/format.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <string>

namespace py = pybind11;

void init_resources_resourcetype(py::module& nw)
{
    nw.def("resource_type_to_string", &nw::ResourceType::to_string);

    py::enum_<nw::ResourceType::type>(nw, "ResourceType")
        .value("invalid", nw::ResourceType::invalid)
        .value("bmp", nw::ResourceType::bmp)
        .value("mve", nw::ResourceType::mve)
        .value("tga", nw::ResourceType::tga)
        .value("wav", nw::ResourceType::wav)
        .value("plt", nw::ResourceType::plt)
        .value("ini", nw::ResourceType::ini)
        .value("bmu", nw::ResourceType::bmu)
        .value("mpg", nw::ResourceType::mpg)
        .value("txt", nw::ResourceType::txt)
        .value("plh", nw::ResourceType::plh)
        .value("tex", nw::ResourceType::tex)
        .value("mdl", nw::ResourceType::mdl)
        .value("thg", nw::ResourceType::thg)
        .value("fnt", nw::ResourceType::fnt)
        .value("lua", nw::ResourceType::lua)
        .value("slt", nw::ResourceType::slt)
        .value("nss", nw::ResourceType::nss)
        .value("ncs", nw::ResourceType::ncs)
        .value("mod", nw::ResourceType::mod)
        .value("are", nw::ResourceType::are)
        .value("set", nw::ResourceType::set)
        .value("ifo", nw::ResourceType::ifo)
        .value("bic", nw::ResourceType::bic)
        .value("wok", nw::ResourceType::wok)
        .value("twoda", nw::ResourceType::twoda)
        .value("tlk", nw::ResourceType::tlk)
        .value("txi", nw::ResourceType::txi)
        .value("git", nw::ResourceType::git)
        .value("bti", nw::ResourceType::bti)
        .value("uti", nw::ResourceType::uti)
        .value("btc", nw::ResourceType::btc)
        .value("utc", nw::ResourceType::utc)
        .value("dlg", nw::ResourceType::dlg)
        .value("itp", nw::ResourceType::itp)
        .value("btt", nw::ResourceType::btt)
        .value("utt", nw::ResourceType::utt)
        .value("dds", nw::ResourceType::dds)
        .value("bts", nw::ResourceType::bts)
        .value("uts", nw::ResourceType::uts)
        .value("ltr", nw::ResourceType::ltr)
        .value("gff", nw::ResourceType::gff)
        .value("fac", nw::ResourceType::fac)
        .value("bte", nw::ResourceType::bte)
        .value("ute", nw::ResourceType::ute)
        .value("btd", nw::ResourceType::btd)
        .value("utd", nw::ResourceType::utd)
        .value("btp", nw::ResourceType::btp)
        .value("utp", nw::ResourceType::utp)
        .value("dft", nw::ResourceType::dft)
        .value("gic", nw::ResourceType::gic)
        .value("gui", nw::ResourceType::gui)
        .value("css", nw::ResourceType::css)
        .value("ccs", nw::ResourceType::ccs)
        .value("btm", nw::ResourceType::btm)
        .value("utm", nw::ResourceType::utm)
        .value("dwk", nw::ResourceType::dwk)
        .value("pwk", nw::ResourceType::pwk)
        .value("btg", nw::ResourceType::btg)
        .value("utg", nw::ResourceType::utg)
        .value("jrl", nw::ResourceType::jrl)
        .value("sav", nw::ResourceType::sav)
        .value("utw", nw::ResourceType::utw)
        .value("fourpc", nw::ResourceType::fourpc)
        .value("ssf", nw::ResourceType::ssf)
        .value("hak", nw::ResourceType::hak)
        .value("nwm", nw::ResourceType::nwm)
        .value("bik", nw::ResourceType::bik)
        .value("ndb", nw::ResourceType::ndb)
        .value("ptm", nw::ResourceType::ptm)
        .value("ptt", nw::ResourceType::ptt)
        .value("bak", nw::ResourceType::bak)
        .value("dat", nw::ResourceType::dat)
        .value("shd", nw::ResourceType::shd)
        .value("xbc", nw::ResourceType::xbc)
        .value("wbm", nw::ResourceType::wbm)
        .value("mtr", nw::ResourceType::mtr)
        .value("ktx", nw::ResourceType::ktx)
        .value("ttf", nw::ResourceType::ttf)
        .value("sql", nw::ResourceType::sql)
        .value("tml", nw::ResourceType::tml)
        .value("sq3", nw::ResourceType::sq3)
        .value("lod", nw::ResourceType::lod)
        .value("gif", nw::ResourceType::gif)
        .value("png", nw::ResourceType::png)
        .value("jpg", nw::ResourceType::jpg)
        .value("caf", nw::ResourceType::caf)
        .value("ids", nw::ResourceType::ids)
        .value("erf", nw::ResourceType::erf)
        .value("bif", nw::ResourceType::bif)
        .value("key", nw::ResourceType::key);
}

void init_resources_resource(py::module& nw)
{
    nw.def("resmatch", [](const nw::Resource& r, std::string_view pat) {
        auto re = nw::string::glob_to_regex(pat);
        return std::regex_match(r.filename(), re);
    });

    py::class_<nw::Resref>(nw, "Resref")
        .def(py::init<>())
        .def(py::init<std::string_view>());

    py::class_<nw::Resource>(nw, "Resource")
        .def(py::init<>())
        .def(py::init<>([](std::string_view name) {
            return nw::Resource::from_filename(name);
        }))
        .def(py::init<std::string_view, nw::ResourceType::type>())
        .def_static("from_filename", nw::Resource::from_filename)
        .def("__repr__", &nw::Resource::filename)
        .def("filename", &nw::Resource::filename)
        .def_readwrite("resref", &nw::Resource::resref)
        .def_readwrite("type", &nw::Resource::type)
        .def("valid", &nw::Resource::valid);
}

void init_resources_descriptor(py::module& nw)
{
    py::class_<nw::ResourceDescriptor>(nw, "ResourceDescriptor")
        .def_readwrite("name", &nw::ResourceDescriptor::name)
        .def_readwrite("size", &nw::ResourceDescriptor::size)
        .def_readwrite("mtime", &nw::ResourceDescriptor::mtime)
        .def_readwrite("parent", &nw::ResourceDescriptor::parent);
}

void init_resources_data(py::module& nw)
{
    py::class_<nw::ResourceData>(nw, "ResourceData")
        .def_readwrite("name", &nw::ResourceData::name)
        .def_readwrite("bytes", &nw::ResourceData::bytes);
}

void init_resources_container(py::module& nw)
{
    py::class_<nw::Container>(nw, "Container")
        .def("all", &nw::Container::all, "Gets resource descriptors for all resources in a container")
        .def("contains", &nw::Container::contains)
        .def("contains", [](const nw::Container& self, std::string_view name) {
            return self.contains(nw::Resource::from_filename(name));
        })
        .def("demand", &nw::Container::demand)
        .def("demand", [](const nw::Container& self, std::string_view name) {
            return self.demand(nw::Resource::from_filename(name));
        })
        .def("extract_by_glob", &nw::Container::extract_by_glob)
        .def("extract", [](nw::Container* self, std::string re, std::filesystem::path& path) {
            return self->extract(std::regex(re), path);
        })
        .def("name", &nw::Container::name)
        .def("path", &nw::Container::path)
        .def("size", &nw::Container::size)
        .def("stat", &nw::Container::stat)
        .def("valid", &nw::Container::valid);
}

void init_resources_dir(py::module& nw)
{
    // virtuals will already have been handled in Container
    py::class_<nw::Directory, nw::Container>(nw, "Directory")
        .def(py::init<std::filesystem::path>());
}

void init_resources_erf(py::module& nw)
{
    // virtuals will already have been handled in Container
    py::class_<nw::Erf, nw::Container>(nw, "Erf")
        .def(py::init<>())
        .def(py::init<std::filesystem::path>())
        //.def("add", static_cast<bool (nw::Erf::*)(nw::Resource res, const nw::ByteArray&)>(&nw::Erf::add))
        .def("add", static_cast<bool (nw::Erf::*)(const std::filesystem::path&)>(&nw::Erf::add))
        .def("erase", &nw::Erf::erase)
        .def("merge", &nw::Erf::merge)
        .def("reload", &nw::Erf::reload)
        .def("save", &nw::Erf::save)
        .def("save_as", &nw::Erf::save_as)

        .def_readwrite("description", &nw::Erf::description);
}

void init_resources_nwsync(py::module& nw)
{
    py::class_<nw::NWSync>(nw, "NWSync")
        .def(py::init<std::filesystem::path>())
        .def("get", &nw::NWSync::get, py::return_value_policy::reference_internal)
        .def("is_loaded", &nw::NWSync::is_loaded)
        .def("manifests", &nw::NWSync::manifests)
        .def("shard_count", &nw::NWSync::shard_count);

    // virtuals will already have been handled in Container
    py::class_<nw::NWSyncManifest, nw::Container>(nw, "NWSyncManifest");
}

void init_resources_key(py::module& nw)
{
    // virtuals will already have been handled in Container
    py::class_<nw::Key, nw::Container>(nw, "Key")
        .def(py::init<std::filesystem::path>());
}

void init_resources_zip(py::module& nw)
{
    // virtuals will already have been handled in Container
    py::class_<nw::Zip, nw::Container>(nw, "Zip")
        .def(py::init<std::filesystem::path>());
}

void init_resources(py::module& nw)
{
    init_resources_data(nw);
    init_resources_resourcetype(nw);
    init_resources_resource(nw);
    init_resources_descriptor(nw);
    init_resources_container(nw);
    init_resources_dir(nw);
    init_resources_erf(nw);
    init_resources_key(nw);
    init_resources_nwsync(nw);
    init_resources_zip(nw);
}
