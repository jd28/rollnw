#include "../stdlib.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../kernel/Strings.hpp"

#include <absl/hash/hash.h>

#include <limits>

namespace nw::smalls {

namespace {

template <typename T>
const T* native_value_ptr(Runtime& rt, const Value& value)
{
    return static_cast<const T*>(rt.get_value_data_ptr(value));
}

template <typename T>
Value native_value_eq(const Value& lhs, const Value& rhs)
{
    auto& rt = nw::kernel::runtime();
    const T* l = native_value_ptr<T>(rt, lhs);
    const T* r = native_value_ptr<T>(rt, rhs);
    return Value::make_bool(l && r && *l == *r);
}

template <typename T>
Value native_value_lt(const Value& lhs, const Value& rhs)
{
    auto& rt = nw::kernel::runtime();
    const T* l = native_value_ptr<T>(rt, lhs);
    const T* r = native_value_ptr<T>(rt, rhs);
    return Value::make_bool(l && r && *l < *r);
}

template <typename T>
Value native_value_hash(const Value& value)
{
    auto& rt = nw::kernel::runtime();
    const T* ptr = native_value_ptr<T>(rt, value);
    return Value::make_int(ptr ? static_cast<int32_t>(static_cast<uint32_t>(absl::HashOf(*ptr))) : 0);
}

nw::Resref resref_from_string(ScriptString value)
{
    auto& rt = nw::kernel::runtime();
    return nw::Resref{value.view(rt)};
}

ScriptString resref_to_string(nw::Resref value)
{
    auto& rt = nw::kernel::runtime();
    return ScriptString{rt.alloc_string(value.view())};
}

nw::Resource make_resource(nw::Resref resref, nw::ResourceType::type type)
{
    return nw::Resource{resref, type};
}

nw::Resref resource_resref(nw::Resource value)
{
    return value.resref;
}

nw::ResourceType::type resource_type(nw::Resource value)
{
    return value.type;
}

bool resource_valid(nw::Resource value)
{
    return value.valid();
}

ScriptString resource_filename(nw::Resource value)
{
    auto& rt = nw::kernel::runtime();
    return ScriptString{rt.alloc_string(value.filename())};
}

bool resource_exists(nw::Resref resref, nw::ResourceType::type type)
{
    return nw::kernel::resman().contains({resref, type});
}

nw::LanguageID language_from_int(int32_t value)
{
    if (value < 0) { return nw::LanguageID::invalid; }
    return static_cast<nw::LanguageID>(static_cast<uint32_t>(value));
}

nw::TextRef make_text_ref(int32_t strref)
{
    uint32_t value = strref < 0
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(strref);
    return nw::kernel::strings().make_text_ref(value);
}

nw::TextRef make_inline_text_ref(ScriptString text, int32_t language)
{
    auto& rt = nw::kernel::runtime();
    nw::TextRef ref = nw::kernel::strings().make_text_ref();
    nw::kernel::strings().set_text(ref, language_from_int(language), text.view(rt));
    return ref;
}

bool set_text(nw::TextRef ref, ScriptString text, int32_t language)
{
    auto& rt = nw::kernel::runtime();
    return nw::kernel::strings().set_text(ref, language_from_int(language), text.view(rt));
}

bool set_text_override(nw::TextRef ref, ScriptString text, int32_t language)
{
    auto& rt = nw::kernel::runtime();
    return nw::kernel::strings().set_text_override(ref, language_from_int(language), text.view(rt));
}

void clear_text_override(nw::TextRef ref, int32_t language)
{
    nw::kernel::strings().clear_text_override(ref, language_from_int(language));
}

ScriptString resolve_text(nw::TextRef ref, int32_t language)
{
    auto& rt = nw::kernel::runtime();
    return ScriptString{rt.alloc_string(nw::kernel::strings().get(ref, language_from_int(language)))};
}

} // namespace

void register_core_types(Runtime& rt)
{
    if (rt.get_native_module("core.types")) {
        return;
    }

    rt.module("core.types")
        .value_type<nw::Resref>("ResRef")
        .value_type<nw::Resource>("Resource")
        .value_type<nw::TextRef>("TextRef")
        .function("__resref_from_string", &resref_from_string)
        .function("resref_to_string", &resref_to_string)
        .function("resource", &make_resource)
        .function("resource_resref", &resource_resref)
        .function("resource_type", &resource_type)
        .function("resource_valid", &resource_valid)
        .function("resource_filename", &resource_filename)
        .function("resource_exists", &resource_exists)
        .function("make_text_ref", &make_text_ref)
        .function("make_inline_text_ref", &make_inline_text_ref)
        .function("set_text", &set_text)
        .function("set_text_override", &set_text_override)
        .function("clear_text_override", &clear_text_override)
        .function("resolve_text", &resolve_text)
        .finalize();

    TypeID resref_type = rt.type_id("core.types.ResRef");
    TypeID resource_type_id = rt.type_id("core.types.Resource");
    TypeID text_ref_type = rt.type_id("core.types.TextRef");
    if (resref_type != invalid_type_id) {
        rt.register_binary_op(TokenType::EQEQ, resref_type, resref_type, rt.bool_type(), native_value_eq<nw::Resref>);
        rt.register_binary_op(TokenType::LT, resref_type, resref_type, rt.bool_type(), native_value_lt<nw::Resref>);
        rt.register_native_hash_op(resref_type, native_value_hash<nw::Resref>);
    }
    if (resource_type_id != invalid_type_id) {
        rt.register_binary_op(TokenType::EQEQ, resource_type_id, resource_type_id, rt.bool_type(), native_value_eq<nw::Resource>);
        rt.register_binary_op(TokenType::LT, resource_type_id, resource_type_id, rt.bool_type(), native_value_lt<nw::Resource>);
        rt.register_native_hash_op(resource_type_id, native_value_hash<nw::Resource>);
    }
    if (text_ref_type != invalid_type_id) {
        rt.register_binary_op(TokenType::EQEQ, text_ref_type, text_ref_type, rt.bool_type(), native_value_eq<nw::TextRef>);
        rt.register_binary_op(TokenType::LT, text_ref_type, text_ref_type, rt.bool_type(), native_value_lt<nw::TextRef>);
        rt.register_native_hash_op(text_ref_type, native_value_hash<nw::TextRef>);
    }
}

} // namespace nw::smalls
