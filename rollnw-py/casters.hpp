#pragma once

#include <nw/kernel/Strings.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/resources/assets.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/items.hpp>
#include <nw/util/ByteArray.hpp>
#include <nw/util/InternedString.hpp>
#include <nw/util/macros.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string_view.h>

// nanobind caster
namespace nanobind::detail {

template <>
struct type_caster<nw::ByteArray> {
public:
    NB_TYPE_CASTER(nw::ByteArray, const_name("ByteArray"))

    bool from_python(handle src, uint8_t, cleanup_list*)
    {
        PyObject* source = src.ptr();
        if (!PyBytes_Check(source)) { return false; }
        value.append(reinterpret_cast<const uint8_t*>(PyBytes_AsString(source)),
            static_cast<size_t>(PyBytes_Size(source)));
        return !PyErr_Occurred();
    }

    static handle from_cpp(const nw::ByteArray& src, rv_policy, cleanup_list*)
    {
        // This may or may not be a good idea...
        object obj = nanobind::bytes(reinterpret_cast<const char*>(src.data()), src.size());
        return obj.release();
    }
};

template <>
struct type_caster<nw::InternedString> {
public:
    NB_TYPE_CASTER(nw::InternedString, const_name("InternedString"))

    bool from_python(handle src, uint8_t, cleanup_list*)
    {
        Py_ssize_t size;
        const char* str = PyUnicode_AsUTF8AndSize(src.ptr(), &size);
        if (!str) {
            PyErr_Clear();
            return false;
        }
        value = nw::kernel::strings().intern({str, static_cast<size_t>(size)});
        return true;
    }

    static handle from_cpp(const nw::InternedString& src, rv_policy, cleanup_list*)
    {
        return PyUnicode_FromStringAndSize(src.view().data(), src.view().size());
    }
};

template <>
struct type_caster<nw::Resref> {
public:
    NB_TYPE_CASTER(nw::Resref, const_name("Resref"))

    bool from_python(handle src, uint8_t, cleanup_list*)
    {
        Py_ssize_t size;
        const char* str = PyUnicode_AsUTF8AndSize(src.ptr(), &size);
        if (!str) {
            PyErr_Clear();
            return false;
        }
        value = nw::Resref({str, static_cast<size_t>(size)});
        return true;
    }

    static handle from_cpp(const nw::Resref& src, rv_policy, cleanup_list*)
    {
        return PyUnicode_FromStringAndSize(src.view().data(), src.view().size());
    }
};

#define DEFINE_RULE_TYPE_CASTER(name)                                         \
    template <>                                                               \
    struct type_caster<nw::name> {                                            \
    public:                                                                   \
        NB_TYPE_CASTER(nw::name, const_name(ROLLNW_STRINGIFY(name)))          \
        bool from_python(handle src, uint8_t, cleanup_list*)                  \
        {                                                                     \
            PyObject* source = src.ptr();                                     \
            if (!PyLong_Check(source)) { return false; }                      \
            value = nw::name{int32_t(PyLong_AsLong(source))};                 \
            return !PyErr_Occurred();                                         \
        }                                                                     \
        static handle from_cpp(const nw::name& src, rv_policy, cleanup_list*) \
        {                                                                     \
            object obj = nanobind::int_(*src);                                \
            return obj.release();                                             \
        }                                                                     \
    };

DEFINE_RULE_TYPE_CASTER(Ability)
DEFINE_RULE_TYPE_CASTER(Appearance)
DEFINE_RULE_TYPE_CASTER(ArmorClass)
DEFINE_RULE_TYPE_CASTER(AttackType)
DEFINE_RULE_TYPE_CASTER(BaseItem)
DEFINE_RULE_TYPE_CASTER(Class)
DEFINE_RULE_TYPE_CASTER(CombatMode)
DEFINE_RULE_TYPE_CASTER(Damage)
DEFINE_RULE_TYPE_CASTER(DamageModType)
DEFINE_RULE_TYPE_CASTER(Disease)
DEFINE_RULE_TYPE_CASTER(EffectType)
DEFINE_RULE_TYPE_CASTER(Feat)
DEFINE_RULE_TYPE_CASTER(ItemPropertyType)
DEFINE_RULE_TYPE_CASTER(MasterFeat)
DEFINE_RULE_TYPE_CASTER(MissChanceType)
DEFINE_RULE_TYPE_CASTER(ModifierType)
DEFINE_RULE_TYPE_CASTER(Phenotype)
DEFINE_RULE_TYPE_CASTER(PlaceableType)
DEFINE_RULE_TYPE_CASTER(Poison)
DEFINE_RULE_TYPE_CASTER(Race)
DEFINE_RULE_TYPE_CASTER(ReqType)
DEFINE_RULE_TYPE_CASTER(Save)
DEFINE_RULE_TYPE_CASTER(SaveVersus)
DEFINE_RULE_TYPE_CASTER(Situation)
DEFINE_RULE_TYPE_CASTER(Skill)
DEFINE_RULE_TYPE_CASTER(SpecialAttack)
DEFINE_RULE_TYPE_CASTER(Spell)
DEFINE_RULE_TYPE_CASTER(SpellSchool)
DEFINE_RULE_TYPE_CASTER(TrapType)

}
