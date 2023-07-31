#include <nw/formats/Ini.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/legacy/Image.hpp>
#include <nw/legacy/Plt.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <string>
#include <variant>

namespace py = pybind11;

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
        .def("data", &nw::Image::data, py::return_value_policy::reference_internal)
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
    init_formats_image(nw);
    init_formats_ini(nw);
    init_formats_plt(nw);
    init_formats_twoda(nw);
}
