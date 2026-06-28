#include "../stdlib.hpp"

#include <absl/hash/hash.h>

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

} // namespace

void register_core_types(Runtime& rt)
{
    if (rt.get_native_module("core.types")) {
        return;
    }

    rt.module("core.types")
        .value_type<nw::Resref>("ResRef")
        .value_type<nw::Resource>("Resource")
        .function("__resref_from_string", &resref_from_string)
        .function("resref_to_string", &resref_to_string)
        .function("resource", &make_resource)
        .function("resource_resref", &resource_resref)
        .function("resource_type", &resource_type)
        .function("resource_valid", &resource_valid)
        .function("resource_filename", &resource_filename)
        .finalize();

    TypeID resref_type = rt.type_id("core.types.ResRef");
    TypeID resource_type_id = rt.type_id("core.types.Resource");
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
}

} // namespace nw::smalls
