#pragma once

#include <nw/kernel/Strings.hpp>
#include <nw/resources/Resref.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/util/ByteArray.hpp>
#include <nw/util/InternedString.hpp>
#include <nw/util/macros.hpp>

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

template <>
struct type_caster<nw::InternedString> {
public:
    PYBIND11_TYPE_CASTER(nw::InternedString, _("InternedString"));

    bool load(handle src, bool)
    {
        PyObject* source = src.ptr();
        if (!PyUnicode_Check(source)) { return false; }
        value = nw::kernel::strings().intern(PyUnicode_AsUTF8(source));
        return !PyErr_Occurred();
    }

    static handle cast(const nw::InternedString& src, return_value_policy /* policy */, handle /* parent */)
    {
        object obj = pybind11::str(src.view());
        return obj.release();
    }
};

template <>
struct type_caster<nw::Resref> {
public:
    PYBIND11_TYPE_CASTER(nw::Resref, _("Resref"));

    bool load(handle src, bool)
    {
        PyObject* source = src.ptr();
        if (!PyUnicode_Check(source)) { return false; }
        value = PyUnicode_AsUTF8(source);
        return !PyErr_Occurred();
    }

    static handle cast(const nw::Resref& src, return_value_policy /* policy */, handle /* parent */)
    {
        object obj = pybind11::str(src.string());
        return obj.release();
    }
};

#define DEFINE_RULE_TYPE_CASTER(name)                                                                  \
    template <>                                                                                        \
    struct type_caster<nw::name> {                                                                     \
    public:                                                                                            \
        PYBIND11_TYPE_CASTER(nw::name, _(ROLLNW_STRINGIFY(name)));                                     \
        bool load(handle src, bool)                                                                    \
        {                                                                                              \
            PyObject* source = src.ptr();                                                              \
            if (!PyLong_Check(source)) { return false; }                                               \
            value = nw::name{int32_t(PyLong_AsLong(source))};                                          \
            return !PyErr_Occurred();                                                                  \
        }                                                                                              \
        static handle cast(const nw::name& src, return_value_policy /* policy */, handle /* parent */) \
        {                                                                                              \
            object obj = pybind11::int_(*src);                                                         \
            return obj.release();                                                                      \
        }                                                                                              \
    };

DEFINE_RULE_TYPE_CASTER(Ability)
DEFINE_RULE_TYPE_CASTER(AttackType)
DEFINE_RULE_TYPE_CASTER(EffectType)
DEFINE_RULE_TYPE_CASTER(Class)
DEFINE_RULE_TYPE_CASTER(Feat)
DEFINE_RULE_TYPE_CASTER(Skill)
DEFINE_RULE_TYPE_CASTER(Spell)
DEFINE_RULE_TYPE_CASTER(Race)

}
