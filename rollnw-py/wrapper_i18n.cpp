#include <nw/i18n/Language.hpp>
#include <nw/i18n/LocString.hpp>
#include <nw/i18n/Tlk.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nlohmann/json.hpp>

#include "json/json.hpp"

#include <filesystem>

namespace py = nanobind;

void init_i18n_language(py::module_& m)
{
    py::enum_<nw::LanguageID>(m, "LanguageID")
        .value("english", nw::LanguageID::english)
        .value("french", nw::LanguageID::french)
        .value("german", nw::LanguageID::german)
        .value("italian", nw::LanguageID::italian)
        .value("spanish", nw::LanguageID::spanish)
        .value("polish", nw::LanguageID::polish)
        .value("korean", nw::LanguageID::korean)
        .value("chinese_traditional", nw::LanguageID::chinese_traditional)
        .value("chinese_simplified", nw::LanguageID::chinese_simplified)
        .value("japanese", nw::LanguageID::japanese);

    py::class_<nw::Language>(m, "Language")
        .def_static("encoding", &nw::Language::encoding)
        .def_static("from_string", &nw::Language::from_string)
        .def_static("has_feminine", &nw::Language::has_feminine)
        .def_static("to_base_id", &nw::Language::to_base_id)
        .def_static("to_runtime_id", &nw::Language::to_runtime_id, py::arg("language"), py::arg("feminine") = false)
        .def_static("to_string", &nw::Language::to_string, py::arg("language"), py::arg("long_name") = false);
}

void init_i18n_locstring(py::module_& m)
{
    py::class_<nw::LocString>(m, "LocString")
        .def(py::init<>())
        .def(py::init<uint32_t>())
        .def(
            "__getitem__",
            [](const nw::LocString& ls, nw::LanguageID lang) {
                return ls.get(lang);
            })
        .def("add", &nw::LocString::add, py::arg("language"), py::arg("string"), py::arg("feminine") = false)
        .def_static("from_dict", [](const nlohmann::json& j) -> nw::LocString {
            nw::LocString ls;
            nw::from_json(j, ls);
            return ls;
        })
        .def("get", &nw::LocString::get, py::arg("language"), py::arg("feminine") = false)
        .def("remove", &nw::LocString::remove, py::arg("language"), py::arg("feminine") = false)
        .def("set_strref", &nw::LocString::set_strref)
        .def("size", &nw::LocString::size)
        .def("strref", [](const nw::LocString& self) { return int(self.strref()); })
        .def("to_dict", [](const nw::LocString& self) -> nlohmann::json {
            nlohmann::json j;
            nw::to_json(j, self);
            return j;
        });
}

void init_i18n_tlk(py::module_& m)
{
    py::class_<nw::Tlk>(m, "Tlk")
        .def(py::init<std::filesystem::path>())
        .def(py::init<nw::LanguageID>())
        .def("__getitem__", [](const nw::Tlk& self, uint32_t strref) {
            return self.get(strref);
        })
        .def("__setitem__", [](nw::Tlk& self, uint32_t strref, std::string_view string) {
            self.set(strref, string);
        })
        .def("__len__", &nw::Tlk::size)
        .def("get", &nw::Tlk::get)
        .def("language_id", &nw::Tlk::language_id)
        .def("modified", &nw::Tlk::modified)
        .def("save", &nw::Tlk::save)
        .def("save_as", &nw::Tlk::save_as)
        .def("set", &nw::Tlk::set)
        .def("size", &nw::Tlk::size)
        .def("valid", &nw::Tlk::valid);
}

void init_i18n(py::module_& m)
{
    init_i18n_language(m);
    init_i18n_locstring(m);
    init_i18n_tlk(m);
}
