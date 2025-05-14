#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/formats/Dialog.hpp>
#include <nw/formats/Image.hpp>
#include <nw/formats/Ini.hpp>
#include <nw/formats/Palette.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/formats/StaticTwoDA.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/util/platform.hpp>

#include "json/json.hpp"
#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nlohmann/json.hpp>

#include <string>
#include <variant>

namespace py = nanobind;

void init_formats_dialog(py::module_& nw)
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
        .def_ro("parent", &nw::DialogPtr::parent, py::rv_policy::reference_internal)
        .def_ro("type", &nw::DialogPtr::type)
        .def_ro("node", &nw::DialogPtr::node, py::rv_policy::reference_internal)
        .def_rw("script_appears", &nw::DialogPtr::script_appears)
        .def_ro("is_start", &nw::DialogPtr::is_start)
        .def_ro("is_link", &nw::DialogPtr::is_link)
        .def_rw("comment", &nw::DialogPtr::comment)
        .def("add_ptr", &nw::DialogPtr::add_ptr,
            py::arg("ptr"),
            py::arg("is_link") = true,
            py::rv_policy::reference_internal)
        .def("add_string", &nw::DialogPtr::add_string,
            py::arg("value"),
            py::arg("lang") = nw::LanguageID::english,
            py::arg("feminine") = false,
            py::rv_policy::reference_internal)
        .def("add", &nw::DialogPtr::add,
            py::rv_policy::reference_internal)
        .def("copy", &nw::DialogPtr::copy,
            py::rv_policy::reference_internal)
        .def("get_condition_param", &nw::DialogPtr::get_condition_param)
        .def("remove_condition_param", [](nw::DialogPtr* ptr, const std::string& key) {
            ptr->remove_condition_param(key);
        })
        .def("remove_ptr", &nw::DialogPtr::remove_ptr)
        .def("set_condition_param", &nw::DialogPtr::set_condition_param);

    py::class_<nw::DialogNode>(nw, "DialogNode")
        .def_ro("parent", &nw::DialogNode::parent, py::rv_policy::reference_internal)
        .def_ro("type", &nw::DialogNode::type)
        .def_rw("comment", &nw::DialogNode::comment)
        .def_rw("quest", &nw::DialogNode::quest)
        .def_rw("speaker", &nw::DialogNode::speaker)
        .def_rw("quest_entry", &nw::DialogNode::quest_entry)
        .def_rw("script_action", &nw::DialogNode::script_action)
        .def_rw("sound", &nw::DialogNode::sound)
        .def_rw("text", &nw::DialogNode::text)
        .def_rw("animation", &nw::DialogNode::animation)
        .def_rw("delay", &nw::DialogNode::delay)
        .def_rw("pointers", &nw::DialogNode::pointers)
        .def("copy", &nw::DialogNode::copy,
            py::rv_policy::reference_internal)
        .def("get_action_param", &nw::DialogNode::get_action_param)
        .def("remove_action_param", [](nw::DialogNode* ptr, const std::string& key) {
            ptr->remove_action_param(key);
        })
        .def("set_action_param", &nw::DialogNode::set_action_param);

    py::class_<nw::Dialog>(nw, "Dialog")
        .def(py::init<>())
        .def_ro_static("json_archive_version", &nw::Dialog::json_archive_version)
        .def_ro_static("restype", &nw::Dialog::restype)
        .def("add_ptr", &nw::Dialog::add_ptr,
            py::arg("ptr"),
            py::arg("is_link") = true,
            py::rv_policy::reference_internal)
        .def("add_string", &nw::Dialog::add_string,
            py::arg("value"),
            py::arg("lang") = nw::LanguageID::english,
            py::arg("feminine") = false,
            py::rv_policy::reference_internal)
        .def("add", &nw::Dialog::add,
            py::rv_policy::reference_internal)
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
        .def_rw("script_abort", &nw::Dialog::script_abort)
        .def_rw("script_end", &nw::Dialog::script_end)
        .def_rw("delay_entry", &nw::Dialog::delay_entry)
        .def_rw("delay_reply", &nw::Dialog::delay_reply)
        .def_rw("word_count", &nw::Dialog::word_count)
        .def_rw("prevent_zoom", &nw::Dialog::prevent_zoom);
}

void init_formats_ini(py::module_& nw)
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

void init_formats_image(py::module_& nw)
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

void init_formats_palette(py::module_& nw)
{
    py::enum_<nw::PaletteNodeType>(nw, "PaletteNodeType")
        .value("branch", nw::PaletteNodeType::branch)
        .value("category", nw::PaletteNodeType::category)
        .value("blueprint", nw::PaletteNodeType::blueprint)

        ;

    py::class_<nw::PaletteTreeNode>(nw, "PaletteTreeNode")
        .def("clear", &nw::PaletteTreeNode::clear)

        .def_rw("type", &nw::PaletteTreeNode::type)
        .def_ro("id", &nw::PaletteTreeNode::id)
        .def_ro("display", &nw::PaletteTreeNode::display)
        .def_ro("name", &nw::PaletteTreeNode::name)
        .def_ro("name", &nw::PaletteTreeNode::name)
        .def_ro("strref", &nw::PaletteTreeNode::strref)
        .def_ro("resref", &nw::PaletteTreeNode::resref)
        .def_ro("cr", &nw::PaletteTreeNode::cr)
        .def_ro("faction", &nw::PaletteTreeNode::faction)
        .def_ro("children", &nw::PaletteTreeNode::children)

        ;

    py::class_<nw::Palette>(nw, "Palette")
        .def("is_skeleton", &nw::Palette::is_skeleton)
        .def("to_dict", &nw::Palette::to_json)
        .def("valid", &nw::Palette::valid)

        .def_ro("resource_type", &nw::Palette::resource_type)
        .def_ro("tileset", &nw::Palette::tileset)
        .def_ro("children", &nw::Palette::children)

        .def("save", [](const nw::Palette& self, const std::string& path, const std::string& format) {
            std::filesystem::path out{path};
            if (nw::string::icmp(format, "gff")) {
                nw::GffBuilder oa = nw::serialize(self);
                oa.write_to(out);
            } else if (nw::string::icmp(format, "json")) {
                nlohmann::json j = self.to_json();
                std::filesystem::path temp_path = std::filesystem::temp_directory_path() / out.filename();
                std::ofstream f{temp_path};
                f << std::setw(4) << j;
                f.close();
                nw::move_file_safely(temp_path, out);
            } else {
                throw std::runtime_error(fmt::format("[palette] unknown format: {}", format));
            } }, py::arg("path"), py::arg("format") = "json")

        .def_static("from_dict", [](const nlohmann::json& json) -> nw::Palette* {
            auto result = new nw::Palette;
            result->from_json(json);
            return result; })

        .def_static("from_file", [](const std::string& path) -> nw::Palette* {
            auto p = nw::expand_path(path);
            if (!std::filesystem::exists(path)) {
                throw std::runtime_error(fmt::format("file '{}' does not exist", path));
            }
            auto rdata = nw::ResourceData::from_file(p);
            if (rdata.bytes.size() == 0) {
                throw std::runtime_error(fmt::format("failed to read file '{}'", path));
            }

            if (nw::string::startswith(rdata.bytes.string_view(), nw::Palette::serial_id)) {
                nw::Gff gff{std::move(rdata)};
                return new nw::Palette(gff);
            } else {
                auto archive = nlohmann::json::parse(rdata.bytes.string_view());
                auto result = new nw::Palette;
                result->from_json(archive);
                return result;
            } })

        ;
}

void init_formats_plt(py::module_& nw)
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
        .def_ro("color", &nw::PltPixel::color)
        .def_ro("layer", &nw::PltPixel::layer);

    py::class_<nw::PltColors>(nw, "PltColors")
        .def(py::init<>())
        .def_ro("colors", &nw::PltColors::data);

    py::class_<nw::Plt>(nw, "Plt")
        .def(py::init<const std::filesystem::path&>())
        .def("height", &nw::Plt::height)
        .def("pixels", &nw::Plt::pixels)
        .def("valid", &nw::Plt::valid)
        .def("width", &nw::Plt::width);

    nw.def("decode_plt_color", &nw::decode_plt_color);
}

void init_formats_twoda(py::module_& nw)
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

    py::class_<nw::StaticTwoDA>(nw, "StaticTwoDA")
        .def(py::init<std::filesystem::path>())
        .def("__getitem__", &nw::StaticTwoDA::row)
        .def("column_index", &nw::StaticTwoDA::column_index)
        .def("columns", &nw::StaticTwoDA::columns)

        .def("get", [](const nw::StaticTwoDA& self, size_t row, size_t col) {
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

        .def("get", [](const nw::StaticTwoDA& self, size_t row, std::string_view col) {
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
        .def("row", &nw::StaticTwoDA::row)
        .def("rows", &nw::StaticTwoDA::rows)
        .def("valid", &nw::StaticTwoDA::is_valid)
        .def_static("from_string", [](std::string_view file) {
            return new nw::StaticTwoDA(file);
        });

    py::class_<nw::TwoDA>(nw, "TwoDA")
        .def(py::init<>())
        .def(py::init<std::filesystem::path>())
        .def("__str__", [](const nw::TwoDA& self) {
            std::stringstream ss;
            ss << self;
            return ss.str();
        })

        .def("add_column", &nw::TwoDA::add_column)
        .def("column_index", &nw::TwoDA::column_index)
        .def("columns", &nw::TwoDA::columns)
        .def("column_names", &nw::TwoDA::column_names)
        .def("get_raw", &nw::TwoDA::get_raw)
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
        .def("rows", &nw::TwoDA::rows)
        .def("valid", &nw::TwoDA::is_valid)
        .def_static("from_string", [](std::string_view file) {
            return new nw::TwoDA(file);
        });
}

void init_formats(py::module_& nw)
{
    init_formats_dialog(nw);
    init_formats_image(nw);
    init_formats_ini(nw);
    init_formats_palette(nw);
    init_formats_plt(nw);
    init_formats_twoda(nw);
}
