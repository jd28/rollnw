#include <nw/formats/Image.hpp>
#include <nw/formats/TwoDA.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <string>
#include <variant>

namespace py = pybind11;

void init_formats_image(py::module& nw)
{
    py::class_<nw::Image>(nw, "Image")
        .def(py::init<const std::filesystem::path&>())
        .def("channels", &nw::Image::channels, "Gets BPP")
        .def("data", &nw::Image::data, py::return_value_policy::reference_internal, "Get raw data")
        .def("height", &nw::Image::height, "Get height")
        .def("valid", &nw::Image::valid, "Determine if successfully loaded.")
        .def("width", &nw::Image::width, "Get width")
        .def("write_to", &nw::Image::write_to, "Write Image to file");
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
        .def(py::init<std::filesystem::path>(),
            "Constructs TwoDA object from a file")

        .def("__getitem__", &nw::TwoDA::row,
            "Gets a row")

        .def("column_index", &nw::TwoDA::column_index,
            "Finds the index of a column, or -1")

        .def("columns", &nw::TwoDA::columns,
            "Get the number of columns")

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

        .def(
            "set", [](nw::TwoDA& self, size_t row, std::string_view col, std::variant<int, float, std::string> val) {
                if (std::holds_alternative<int>(val)) {
                    self.set(row, col, std::get<int>(val));
                } else if (std::holds_alternative<float>(val)) {
                    self.set(row, col, std::get<float>(val));
                } else if (std::holds_alternative<std::string>(val)) {
                    self.set(row, col, std::get<std::string>(val));
                }
            },
            R"(Set a 2da value

            Args:
                row (int): The first parameter.
                column (str): The second parameter.

            Returns:
                None
            )")
        .def("set", [](nw::TwoDA& self, size_t row, size_t col, std::variant<int, float, std::string> val) {
            if (std::holds_alternative<int>(val)) {
                self.set(row, col, std::get<int>(val));
            } else if (std::holds_alternative<float>(val)) {
                self.set(row, col, std::get<float>(val));
            } else if (std::holds_alternative<std::string>(val)) {
                self.set(row, col, std::get<std::string>(val));
            }
        })

        .def("pad", &nw::TwoDA::pad, "Pads the 2da with ``count`` rows")

        .def("row", &nw::TwoDA::row, "Gets a row")

        .def("rows", &nw::TwoDA::rows, "Number of rows")

        .def("valid", &nw::TwoDA::is_valid, " Is the 2da parsed without error");
}

void init_formats(py::module& nw)
{
    init_formats_image(nw);
    init_formats_twoda(nw);
}
