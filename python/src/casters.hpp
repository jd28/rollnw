#pragma once

#include <nw/util/ByteArray.hpp>

#include <pybind11/pybind11.h>

// pybind11 caster
namespace pybind11::detail {

template <>
struct type_caster<nw::ByteArray> {
public:
    PYBIND11_TYPE_CASTER(nw::ByteArray, _("ByteArray"));

    bool load(handle src, bool)
    {
        PyObject* source = src.ptr();
        if (!PyBytes_Check(source)) { return false; }
        value.append(reinterpret_cast<const uint8_t*>(PyBytes_AsString(source)),
            static_cast<size_t>(PyBytes_Size(source)));
        return !PyErr_Occurred();
    }

    static handle cast(const nw::ByteArray& src, return_value_policy /* policy */, handle /* parent */)
    {
        // This may or may not be a good idea...
        object obj = pybind11::bytes(reinterpret_cast<const char*>(src.data()), src.size());
        return obj.release();
    }
};

}
