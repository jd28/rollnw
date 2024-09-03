#include "opaque_types.hpp"

#include <nw/formats/Dialog.hpp>
#include <nw/formats/Image.hpp>
#include <nw/formats/Ini.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/util/platform.hpp>

#include <nlohmann/json.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/stl_bind.h>

#include <string>
#include <variant>

namespace py = pybind11;

void init_formats_dialog(py::module& nw)
{
    py::enum_<nw::DialogNodeType>(nw, "DialogNodeType")
        .value("entry", nw::DialogNodeType::entry)
        .value("reply", nw::DialogNodeType::reply);

    py::enum_<nw::DialogAnimation>(nw, "DialogAnimation")
        .value("default", nw::DialogAnimation::default_)
        .value("taunt", nw::DialogAnimation::taunt)
        .value("greeting", nw::DialogAnimation::greeting)
        .value("listen", nw::DialogAnimation::listen)
        .value("worship", nw::DialogAnimation::worship)
        .value("salute", nw::DialogAnimation::salute)
        .value("bow", nw::DialogAnimation::bow)
        .value("steal", nw::DialogAnimation::steal)
        .value("talk_normal", nw::DialogAnimation::talk_normal)
        .value("talk_pleading", nw::DialogAnimation::talk_pleading)
        .value("talk_forceful", nw::DialogAnimation::talk_forceful)
        .value("talk_laugh", nw::DialogAnimation::talk_laugh)
        .value("victory_1", nw::DialogAnimation::victory_1)
        .value("victory_2", nw::DialogAnimation::victory_2)
        .value("victory_3", nw::DialogAnimation::victory_3)
        .value("look_far", nw::DialogAnimation::look_far)
        .value("drink", nw::DialogAnimation::drink)
        .value("read", nw::DialogAnimation::read)
        .value("none", nw::DialogAnimation::none);

    py::class_<nw::DialogPtr>(nw, "DialogPtr")
        .def_readonly("parent", &nw::DialogPtr::parent, py::return_value_policy::reference_internal)
        .def_readonly("type", &nw::DialogPtr::type)
        .def_readonly("node", &nw::DialogPtr::node, py::return_value_policy::reference_internal)
        .def_readwrite("script_appears", &nw::DialogPtr::script_appears)
        .def_readonly("is_start", &nw::DialogPtr::is_start)
        .def_readonly("is_link", &nw::DialogPtr::is_link)
        .def_readwrite("comment", &nw::DialogPtr::comment)
        .def("add_ptr", &nw::DialogPtr::add_ptr,
            py::arg("ptr"),
            py::arg("is_link") = true,
            py::return_value_policy::reference_internal)
        .def("add_string", &nw::DialogPtr::add_string,
            py::arg("value"),
            py::arg("lang") = nw::LanguageID::english,
            py::arg("feminine") = false,
            py::return_value_policy::reference_internal)
        .def("add", &nw::DialogPtr::add,
            py::return_value_policy::reference_internal)
        .def("copy", &nw::DialogPtr::copy,
            py::return_value_policy::reference_internal)
        .def("get_condition_param", &nw::DialogPtr::get_condition_param)
        .def("remove_condition_param", [](nw::DialogPtr* ptr, const std::string& key) {
            ptr->remove_condition_param(key);
        })
        .def("remove_ptr", &nw::DialogPtr::remove_ptr)
        .def("set_condition_param", &nw::DialogPtr::set_condition_param);

    py::class_<nw::DialogNode>(nw, "DialogNode")
        .def_readonly("parent", &nw::DialogNode::parent, py::return_value_policy::reference_internal)
        .def_readonly("type", &nw::DialogNode::type)
        .def_readwrite("comment", &nw::DialogNode::comment)
        .def_readwrite("quest", &nw::DialogNode::quest)
        .def_readwrite("speaker", &nw::DialogNode::speaker)
        .def_readwrite("quest_entry", &nw::DialogNode::quest_entry)
        .def_readwrite("script_action", &nw::DialogNode::script_action)
        .def_readwrite("sound", &nw::DialogNode::sound)
        .def_readwrite("text", &nw::DialogNode::text)
        .def_readwrite("animation", &nw::DialogNode::animation)
        .def_readwrite("delay", &nw::DialogNode::delay)
        .def_readwrite("pointers", &nw::DialogNode::pointers)
        .def("copy", &nw::DialogNode::copy,
            py::return_value_policy::reference_internal)
        .def("get_action_param", &nw::DialogNode::get_action_param)
        .def("remove_action_param", [](nw::DialogNode* ptr, const std::string& key) {
            ptr->remove_action_param(key);
        })
        .def("set_action_param", &nw::DialogNode::set_action_param);

    py::class_<nw::Dialog>(nw, "Dialog")
        .def(py::init<>())
        .def_readonly_static("json_archive_version", &nw::Dialog::json_archive_version)
        .def_readonly_static("restype", &nw::Dialog::restype)
        .def("add_ptr", &nw::Dialog::add_ptr,
            py::arg("ptr"),
            py::arg("is_link") = true,
            py::return_value_policy::reference_internal)
        .def("add_string", &nw::Dialog::add_string,
            py::arg("value"),
            py::arg("lang") = nw::LanguageID::english,
            py::arg("feminine") = false,
            py::return_value_policy::reference_internal)
        .def("add", &nw::Dialog::add,
            py::return_value_policy::reference_internal)
        .def("delete_ptr", &nw::Dialog::delete_ptr)
        .def("remove_ptr", &nw::Dialog::remove_ptr)
        .def("save", [](const nw::Dialog& self, const std::string& path) {
            auto ext = nw::complete_file_suffix(path);
            std::filesystem::path out{path};
            if (nw::string::icmp(ext, ".dlg")) {
                nw::GffBuilder oa = nw::serialize(&self);
                oa.write_to(out);
            } else if (nw::string::icmp(ext, ".dlg.json")) {
                nlohmann::json j;
                nw::serialize(j, self);
                std::filesystem::path temp_path = std::filesystem::temp_directory_path() / out.filename();
                std::ofstream f{temp_path};
                f << std::setw(4) << j;
                f.close();
                nw::move_file_safely(temp_path, out);
            } else {
                throw std::runtime_error(fmt::format("[dialog] unknown file extension: {}", ext));
            }
        })
        .def("valid", &nw::Dialog::valid)
        .def("__len__", [](const nw::Dialog* self) {
            return self ? self->starts.size() : 0;
        })
        .def("__getitem__", [](const nw::Dialog* self, size_t index) {
            return (self && index < self->starts.size()) ? self->starts[index] : nullptr;
        })
        .def_static("from_file", [](const std::string& path) {
            auto p = nw::expand_path(path);
            if (!std::filesystem::exists(path)) {
                throw std::runtime_error(fmt::format("{} does not exist", path));
            }
            auto ext = nw::path_to_string(p.extension());
            if (nw::string::icmp(ext, ".json")) {
                std::ifstream f(p);
                nlohmann::json data = nlohmann::json::parse(f);
                auto obj = new nw::Dialog;
                nw::deserialize(data, *obj);
                return obj;
            } else if (nw::ResourceType::from_extension(ext) == nw::Dialog::restype) {
                nw::Gff data{p};
                auto obj = new nw::Dialog(data.toplevel());
                return obj;
            }
            throw std::runtime_error(fmt::format("unknown file extension: {}", ext));
        })
        .def_readwrite("script_abort", &nw::Dialog::script_abort)
        .def_readwrite("script_end", &nw::Dialog::script_end)
        .def_readwrite("delay_entry", &nw::Dialog::delay_entry)
        .def_readwrite("delay_reply", &nw::Dialog::delay_reply)
        .def_readwrite("word_count", &nw::Dialog::word_count)
        .def_readwrite("prevent_zoom", &nw::Dialog::prevent_zoom);
}

void init_formats_ini(py::module& nw)
{
    py::class_<nw::Ini>(nw, "Ini")
        .def(py::init<const std::filesystem::path&>())
        .def("get_int", [](const nw::Ini& self, const char* key) {
            return self.get<int>(key);
        })
        .def("get_float", [](const nw::Ini& self, const char* key) {
            return self.get<float>(key);
        })
        .def("get_str", [](const nw::Ini& self, const char* key) {
            return self.get<std::string>(key);
        })
        .def("valid", &nw::Ini::valid);
}

void init_formats_image(py::module& nw)
{
    py::class_<nw::Image>(nw, "Image")
        .def(py::init<const std::filesystem::path&>())
        .def("channels", &nw::Image::channels)
        .def("data", [](const nw::Image& self) {
            size_t size = self.width() * self.height() * self.channels();
            return py::bytes(reinterpret_cast<char*>(self.data()), size);
        })
        .def("height", &nw::Image::height)
        .def("valid", &nw::Image::valid)
        .def("width", &nw::Image::width)
        .def("write_to", &nw::Image::write_to);
}

void init_formats_plt(py::module& nw)
{
    py::enum_<nw::PltLayer>(nw, "PltLayer")
        .value("skin", nw::plt_layer_skin)
        .value("hair", nw::plt_layer_hair)
        .value("metal1", nw::plt_layer_metal1)
        .value("metal2", nw::plt_layer_metal2)
        .value("cloth1", nw::plt_layer_cloth1)
        .value("cloth2", nw::plt_layer_cloth2)
        .value("leather1", nw::plt_layer_leather1)
        .value("leather2", nw::plt_layer_leather2)
        .value("tattoo1", nw::plt_layer_tattoo1)
        .value("tattoo2", nw::plt_layer_tattoo2);

    py::class_<nw::PltPixel>(nw, "PltPixel")
        .def_readonly("color", &nw::PltPixel::color)
        .def_readonly("layer", &nw::PltPixel::layer);

    py::class_<nw::PltColors>(nw, "PltColors")
        .def(py::init<>())
        .def_readonly("colors", &nw::PltColors::data);

    py::class_<nw::Plt>(nw, "Plt")
        .def(py::init<const std::filesystem::path&>())
        .def("height", &nw::Plt::height)
        .def("pixels", &nw::Plt::pixels)
        .def("valid", &nw::Plt::valid)
        .def("width", &nw::Plt::width);

    nw.def("decode_plt_color", &nw::decode_plt_color);
}

void init_formats_twoda(py::module& nw)
{
    py::class_<nw::TwoDARowView>(nw, "TwoDARowView")
        .def("__getitem__", [](const nw::TwoDARowView& self, size_t col) {
            std::variant<int, float, std::string> result = "";
            if (auto i = self.get<int>(col)) {
                result = *i;
            } else if (auto f = self.get<float>(col)) {
                result = *f;
            } else if (auto s = self.get<std::string>(col)) {
                result = std::move(*s);
            }
            return result;
        })
        .def("__getitem__", [](const nw::TwoDARowView& self, std::string_view col) {
            std::variant<int, float, std::string> result = "";
            if (auto i = self.get<int>(col)) {
                result = *i;
            } else if (auto f = self.get<float>(col)) {
                result = *f;
            } else if (auto s = self.get<std::string>(col)) {
                result = std::move(*s);
            }
            return result;
        })
        .def("size", &nw::TwoDARowView::size);

    py::class_<nw::TwoDA>(nw, "TwoDA")
        .def(py::init<std::filesystem::path>())
        .def("__getitem__", &nw::TwoDA::row)
        .def("column_index", &nw::TwoDA::column_index)
        .def("columns", &nw::TwoDA::columns)

        .def("get", [](const nw::TwoDA& self, size_t row, size_t col) {
            std::variant<int, float, std::string> result = "";
            if (auto i = self.get<int>(row, col)) {
                result = *i;
            } else if (auto f = self.get<float>(row, col)) {
                result = *f;
            } else if (auto s = self.get<std::string>(row, col)) {
                result = std::move(*s);
            }
            return result;
        })

        .def("get", [](const nw::TwoDA& self, size_t row, std::string_view col) {
            std::variant<int, float, std::string> result = "";
            if (auto i = self.get<int>(row, col)) {
                result = *i;
            } else if (auto f = self.get<float>(row, col)) {
                result = *f;
            } else if (auto s = self.get<std::string>(row, col)) {
                result = std::move(*s);
            }
            return result;
        })

        .def("set", [](nw::TwoDA& self, size_t row, std::string_view col, std::variant<int, float, std::string> val) {
            if (std::holds_alternative<int>(val)) {
                self.set(row, col, std::get<int>(val));
            } else if (std::holds_alternative<float>(val)) {
                self.set(row, col, std::get<float>(val));
            } else if (std::holds_alternative<std::string>(val)) {
                self.set(row, col, std::get<std::string>(val));
            }
        })
        .def("set", [](nw::TwoDA& self, size_t row, size_t col, std::variant<int, float, std::string> val) {
            if (std::holds_alternative<int>(val)) {
                self.set(row, col, std::get<int>(val));
            } else if (std::holds_alternative<float>(val)) {
                self.set(row, col, std::get<float>(val));
            } else if (std::holds_alternative<std::string>(val)) {
                self.set(row, col, std::get<std::string>(val));
            }
        })
        .def("pad", &nw::TwoDA::pad)
        .def("row", &nw::TwoDA::row)
        .def("rows", &nw::TwoDA::rows)
        .def("valid", &nw::TwoDA::is_valid);
}

void init_formats(py::module& nw)
{
    init_formats_dialog(nw);
    init_formats_image(nw);
    init_formats_ini(nw);
    init_formats_plt(nw);
    init_formats_twoda(nw);
}
