#include "pybind11_json/pybind11_json.hpp"

#include <nw/i18n/Language.hpp>
#include <nw/i18n/LocString.hpp>
#include <nw/i18n/Tlk.hpp>

#include <nlohmann/json.hpp>
#include <pybind11/pybind11.h>

#include <filesystem>

namespace py = pybind11;

void init_i18n_language(py::module& m)
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
        .def("encoding", &nw::Language::encoding)
        .def("from_string", &nw::Language::from_string)
        .def("has_feminine", &nw::Language::has_feminine)
        .def("to_base_id", &nw::Language::to_base_id)
        .def("to_runtime_id", &nw::Language::to_runtime_id)
        .def("to_string", &nw::Language::to_string);
}

void init_i18n_locstring(py::module& m)
{
    py::class_<nw::LocString>(m, "LocString")
        .def(py::init<uint32_t>())
        .def(
            "__getitem__",
            [](const nw::LocString& ls, nw::LanguageID lang) {
                return ls.get(lang);
            },
            "Gets a localized string.  Note: doesn't account for gender")
        .def("add", &nw::LocString::add, py::arg("language"), py::arg("string"), py::arg("feminine") = false)
        .def_static("from_dict", [](const nlohmann::json& j) -> nw::LocString {
            nw::LocString ls;
            nw::from_json(j, ls);
            return ls;
        })
        .def("get", &nw::LocString::get, "Gets a localized string.")
        .def("size", &nw::LocString::size, "Gets number of localized strings")
        .def("strref", &nw::LocString::strref)
        .def("to_dict", [](const nw::LocString& self) -> nlohmann::json {
            nlohmann::json j;
            nw::to_json(j, self);
            return j;
        });
}

void init_i18n_tlk(py::module& m)
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
        .def("__len__", &nw::Tlk::size, "Gets the highest set strref")
        .def("get", &nw::Tlk::get, "Gets a localized string")
        .def("language_id", &nw::Tlk::language_id, "Gets the language ID")
        .def("modified", &nw::Tlk::modified, "Is Tlk modfied")
        .def("save", &nw::Tlk::save, "Writes TLK to file")
        .def("save_as", &nw::Tlk::save_as, "Writes TLK to file")
        .def("set", &nw::Tlk::set, "Sets a localized string")
        .def("size", &nw::Tlk::size, "Gets the highest set strref")
        .def("valid", &nw::Tlk::valid, "Gets if successfully parsed");
}

void init_i18n(py::module& m)
{
    init_i18n_language(m);
    init_i18n_locstring(m);
    init_i18n_tlk(m);
}
