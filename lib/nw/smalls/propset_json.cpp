#include "propset_json.hpp"

#include "Array.hpp"
#include "runtime.hpp"

#include "../kernel/Strings.hpp"
#include "../log.hpp"
#include "../resources/assets.hpp"
#include "../util/HandlePool.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <string>
#include <utility>

namespace nw::smalls {

namespace {

const Type* storage_type(Runtime* rt, TypeID type_id)
{
    const Type* type = rt->get_type(type_id);
    while (type
        && (type->type_kind == TK_newtype || type->type_kind == TK_alias)
        && type->type_params[0].is<TypeID>()) {
        type_id = type->type_params[0].as<TypeID>();
        type = rt->get_type(type_id);
    }
    return type;
}

bool is_resref_type(Runtime* rt, TypeID type_id)
{
    return type_id == rt->type_id("core.types.ResRef", false);
}

bool is_text_ref_type(Runtime* rt, TypeID type_id)
{
    return type_id == rt->type_id("core.types.TextRef", false);
}

bool json_to_int32(const nlohmann::json& jv, int32_t& out)
{
    if (jv.is_number_unsigned()) {
        uint64_t value = jv.get<uint64_t>();
        if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            return false;
        }
        out = static_cast<int32_t>(value);
        return true;
    }

    if (!jv.is_number_integer()) {
        return false;
    }

    int64_t value = jv.get<int64_t>();
    if (value < static_cast<int64_t>(std::numeric_limits<int32_t>::min())
        || value > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }
    out = static_cast<int32_t>(value);
    return true;
}

bool json_to_uint64(const nlohmann::json& jv, uint64_t& out)
{
    if (jv.is_number_unsigned()) {
        out = jv.get<uint64_t>();
        return true;
    }

    if (!jv.is_number_integer()) {
        return false;
    }

    int64_t value = jv.get<int64_t>();
    if (value < 0) {
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

nlohmann::json value_to_json(Runtime* rt, const Value& value, TypeID declared_type);
bool value_from_json(Runtime* rt, TypeID declared_type, const nlohmann::json& jv, Value& out);
bool struct_from_json(Runtime* rt, const nlohmann::json& j, const Value& value, const StructDef* def);

nlohmann::json array_to_json(Runtime* rt, const IArray& arr)
{
    nlohmann::json result = nlohmann::json::array();
    TypeID element_type = arr.element_type();
    for (size_t i = 0; i < arr.size(); ++i) {
        Value value;
        if (arr.get_value(i, value, *rt)) {
            result.push_back(value_to_json(rt, value, element_type));
        }
    }
    return result;
}

bool array_from_json(Runtime* rt, const nlohmann::json& jv, TypeID array_type_id, Value& out)
{
    if (!jv.is_array()) {
        return false;
    }

    const Type* array_type = rt->get_type(array_type_id);
    if (!array_type || array_type->type_kind != TK_array || !array_type->type_params[0].is<TypeID>()) {
        return false;
    }

    TypeID element_type = array_type->type_params[0].as<TypeID>();
    HeapPtr ptr = rt->create_array_typed(element_type, jv.size());
    if (ptr.value == 0) {
        return false;
    }

    IArray* arr = rt->get_array_typed(ptr);
    if (!arr) {
        return false;
    }

    bool ok = true;
    arr->reserve(jv.size());
    for (const auto& elem : jv) {
        Value value;
        if (value_from_json(rt, element_type, elem, value)) {
            arr->append_value(value, *rt);
        } else {
            ok = false;
        }
    }

    out = Value::make_heap(ptr, rt->heap_.get_header(ptr)->type_id);
    return ok;
}

const Type* fixed_array_type(Runtime* rt, TypeID type_id)
{
    const Type* type = storage_type(rt, type_id);
    if (!type || type->type_kind != TK_fixed_array
        || !type->type_params[0].is<TypeID>()
        || !type->type_params[1].is<int32_t>()) {
        return nullptr;
    }
    return type;
}

nlohmann::json fixed_array_to_json(Runtime* rt, const Value& ref, const FieldDef& fd)
{
    const Type* array_type = fixed_array_type(rt, fd.type_id);
    if (!array_type) {
        return nullptr;
    }

    TypeID element_type = array_type->type_params[0].as<TypeID>();
    int32_t count = array_type->type_params[1].as<int32_t>();
    const Type* element_storage_type = storage_type(rt, element_type);
    if (!element_storage_type || count < 0) {
        return nullptr;
    }

    nlohmann::json result = nlohmann::json::array();
    for (int32_t i = 0; i < count; ++i) {
        Value value = rt->read_value_field_at_offset(ref,
            fd.offset + static_cast<uint32_t>(i) * element_storage_type->size,
            element_type);
        result.push_back(value_to_json(rt, value, element_type));
    }
    return result;
}

bool fixed_array_from_json(Runtime* rt, const nlohmann::json& jv, const Value& ref,
    const FieldDef& fd)
{
    if (!jv.is_array()) {
        return false;
    }

    const Type* array_type = fixed_array_type(rt, fd.type_id);
    if (!array_type) {
        return false;
    }

    TypeID element_type = array_type->type_params[0].as<TypeID>();
    int32_t count = array_type->type_params[1].as<int32_t>();
    const Type* element_storage_type = storage_type(rt, element_type);
    if (!element_storage_type || count < 0
        || jv.size() > static_cast<size_t>(count)) {
        return false;
    }

    bool ok = true;
    int32_t n = static_cast<int32_t>(jv.size());
    for (int32_t i = 0; i < n; ++i) {
        Value value;
        if (!value_from_json(rt, element_type, jv[static_cast<size_t>(i)], value)) {
            ok = false;
            continue;
        }
        if (!rt->write_value_field_at_offset(ref,
                fd.offset + static_cast<uint32_t>(i) * element_storage_type->size,
                element_type,
                value)) {
            ok = false;
        }
    }
    return ok;
}

bool value_from_json(Runtime* rt, TypeID declared_type, const nlohmann::json& jv, Value& out)
{
    const Type* type = storage_type(rt, declared_type);
    if (!type) {
        return false;
    }

    if (type->type_kind == TK_primitive) {
        switch (type->primitive_kind) {
        case PK_int: {
            int32_t value = 0;
            if (!json_to_int32(jv, value)) { return false; }
            out = Value::make_int(value);
            out.type_id = declared_type;
            return true;
        }
        case PK_float:
            if (!jv.is_number()) { return false; }
            out = Value::make_float(jv.get<float>());
            out.type_id = declared_type;
            return true;
        case PK_bool:
            if (!jv.is_boolean()) { return false; }
            out = Value::make_bool(jv.get<bool>());
            out.type_id = declared_type;
            return true;
        case PK_string: {
            if (!jv.is_string()) { return false; }
            std::string value = jv.get<std::string>();
            out = Value::make_string(rt->alloc_string(nw::StringView{value}));
            out.type_id = declared_type;
            return true;
        }
        default:
            return false;
        }
    }

    if (rt->is_native_value_type(declared_type)) {
        if (is_resref_type(rt, declared_type) && jv.is_string()) {
            std::string value = jv.get<std::string>();
            out = detail::make_value(rt, nw::Resref{nw::StringView{value}});
            return out.type_id != invalid_type_id;
        }
        if (is_text_ref_type(rt, declared_type)) {
            nw::TextRef ref;
            if (jv.is_object()) {
                nw::LocString loc;
                try {
                    loc = jv.get<nw::LocString>();
                } catch (const nlohmann::json::exception&) {
                    return false;
                }
                ref = nw::kernel::strings().make_text_ref(loc);
            } else if (jv.is_string()) {
                ref = nw::kernel::strings().make_text_ref();
                nw::kernel::strings().set_text(ref, nw::kernel::strings().global_language(),
                    jv.get<std::string>());
            } else {
                int32_t strref = 0;
                if (!json_to_int32(jv, strref)) { return false; }
                uint32_t value = strref < 0
                    ? std::numeric_limits<uint32_t>::max()
                    : static_cast<uint32_t>(strref);
                ref = nw::kernel::strings().make_text_ref(value);
            }
            out = detail::make_value(rt, ref);
            return out.type_id != invalid_type_id;
        }
        return false;
    }

    if (rt->is_object_like_type(declared_type)) {
        uint64_t raw = 0;
        if (!json_to_uint64(jv, raw)) { return false; }
        out = Value::make_object(ObjectHandle::from_ull(raw));
        out.type_id = declared_type;
        return true;
    }

    if (type->type_kind == TK_array) {
        return array_from_json(rt, jv, declared_type, out);
    }

    if (type->type_kind == TK_struct) {
        if (!jv.is_object()) {
            return false;
        }

        HeapPtr ptr = rt->heap_.allocate(type->size, type->alignment, declared_type);
        auto* data = static_cast<uint8_t*>(rt->heap_.get_ptr(ptr));
        if (!data) {
            return false;
        }

        rt->initialize_zero_defaults(declared_type, data);
        Value value = Value::make_heap(ptr, declared_type);
        if (!struct_from_json(rt, jv, value, rt->get_struct_def(declared_type))) {
            return false;
        }

        out = value;
        return true;
    }

    return false;
}

nlohmann::json struct_to_json(Runtime* rt, const Value& value, const StructDef* def)
{
    nlohmann::json j = nlohmann::json::object();
    if (!def) {
        return j;
    }

    for (uint32_t i = 0; i < def->field_count; ++i) {
        const FieldDef& fd = def->fields[i];
        if (fd.is_unmanaged_array) {
            j[std::string{fd.name.view()}] = nullptr;
            continue;
        }
        if (fixed_array_type(rt, fd.type_id)) {
            j[std::string{fd.name.view()}] = fixed_array_to_json(rt, value, fd);
            continue;
        }
        Value field_value = rt->read_value_field_at_offset(value, fd.offset, fd.type_id);
        j[std::string{fd.name.view()}] = value_to_json(rt, field_value, fd.type_id);
    }
    return j;
}

nlohmann::json value_to_json(Runtime* rt, const Value& value, TypeID declared_type)
{
    const Type* type = storage_type(rt, declared_type);
    if (!type) {
        return nullptr;
    }

    if (type->type_kind == TK_primitive) {
        switch (type->primitive_kind) {
        case PK_int:
            return value.data.ival;
        case PK_float:
            return value.data.fval;
        case PK_bool:
            return value.data.bval;
        case PK_string:
            return std::string{ScriptString{value.data.hptr}.view(*rt)};
        default:
            return nullptr;
        }
    }

    if (rt->is_native_value_type(declared_type)) {
        if (is_resref_type(rt, declared_type)) {
            auto* ptr = static_cast<nw::Resref*>(rt->get_value_data_ptr(value));
            return ptr ? ptr->string() : "";
        }
        if (is_text_ref_type(rt, declared_type)) {
            auto* ptr = static_cast<nw::TextRef*>(rt->get_value_data_ptr(value));
            return ptr ? nlohmann::json(nw::kernel::strings().to_locstring(*ptr)) : nlohmann::json(nw::LocString{});
        }
        return nullptr;
    }

    if (rt->is_object_like_type(declared_type)) {
        return value.data.oval.to_ull();
    }

    if (type->type_kind == TK_array) {
        if (value.storage != ValueStorage::heap || value.data.hptr.value == 0) {
            return nlohmann::json::array();
        }
        IArray* arr = rt->get_array_typed(value.data.hptr);
        return arr ? array_to_json(rt, *arr) : nlohmann::json::array();
    }

    if (type->type_kind == TK_struct) {
        return struct_to_json(rt, value, rt->get_struct_def(declared_type));
    }

    return nullptr;
}

bool struct_from_json(Runtime* rt, const nlohmann::json& j, const Value& value, const StructDef* def)
{
    if (!def || !j.is_object()) {
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0; i < def->field_count; ++i) {
        const FieldDef& fd = def->fields[i];
        const std::string key{fd.name.view()};
        if (!j.contains(key) || j.at(key).is_null()) {
            continue;
        }

        if (fixed_array_type(rt, fd.type_id)) {
            if (!fixed_array_from_json(rt, j.at(key), value, fd)) {
                ok = false;
            }
            continue;
        }

        Value field_value;
        if (!value_from_json(rt, fd.type_id, j.at(key), field_value)) {
            ok = false;
            continue;
        }
        if (!rt->write_value_field_at_offset(value, fd.offset, fd.type_id, field_value)) {
            ok = false;
        }
    }
    return ok;
}

} // namespace

JsonSerializer::JsonSerializer(Runtime* rt)
    : rt_(rt)
{
}

nlohmann::json JsonSerializer::serialize_value(const Value& value, TypeID declared_type) const
{
    return value_to_json(rt_, value, declared_type);
}

bool JsonSerializer::deserialize_value(const nlohmann::json& j, TypeID declared_type, Value& out) const
{
    return value_from_json(rt_, declared_type, j, out);
}

// ---------------------------------------------------------------------------
// Serialize
// ---------------------------------------------------------------------------

nlohmann::json JsonSerializer::serialize_struct(const Value& ref,
    const StructDef* def) const
{
    nlohmann::json j = nlohmann::json::object();
    if (!def) { return j; }

    for (uint32_t i = 0; i < def->field_count; ++i) {
        const FieldDef& fd = def->fields[i];
        const std::string key{fd.name.view()};

        if (fd.is_unmanaged_array) {
            Value arr_val = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
            TypedHandle h = TypedHandle::from_ull(arr_val.data.handle);
            IArray* arr = rt_->object_pool().get_unmanaged_array(h);
            if (!arr) {
                j[key] = nlohmann::json::array();
                continue;
            }
            nlohmann::json ja = nlohmann::json::array();
            TypeID element_type = arr->element_type();
            const Type* element_storage_type = storage_type(rt_, element_type);
            const StructDef* element_struct_def = rt_->get_struct_def(element_type);
            for (size_t k = 0; k < arr->size(); ++k) {
                Value v;
                if (arr->get_value(k, v, *rt_)) {
                    if (element_storage_type && element_storage_type->type_kind == TK_struct) {
                        ja.push_back(struct_to_json(rt_, v, element_struct_def));
                    } else {
                        ja.push_back(value_to_json(rt_, v, element_type));
                    }
                }
            }
            j[key] = std::move(ja);
            continue;
        }

        if (fixed_array_type(rt_, fd.type_id)) {
            j[key] = fixed_array_to_json(rt_, ref, fd);
            continue;
        }

        Value value = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
        j[key] = value_to_json(rt_, value, fd.type_id);
    }
    return j;
}

// ---------------------------------------------------------------------------
// Deserialize
// ---------------------------------------------------------------------------

bool JsonSerializer::deserialize_struct(const nlohmann::json& j,
    const Value& ref, const StructDef* def) const
{
    if (!def) { return false; }
    bool ok = true;

    for (uint32_t i = 0; i < def->field_count; ++i) {
        const FieldDef& fd = def->fields[i];
        const std::string key{fd.name.view()};
        if (!j.contains(key)) { continue; }
        const nlohmann::json& jv = j.at(key);
        if (jv.is_null()) { continue; } // skip null placeholders

        if (fd.is_unmanaged_array) {
            if (!jv.is_array()) {
                ok = false;
                continue;
            }
            Value arr_val = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
            TypedHandle h = TypedHandle::from_ull(arr_val.data.handle);
            IArray* arr = rt_->object_pool().get_unmanaged_array(h);
            if (!arr) {
                ok = false;
                continue;
            }
            arr->clear(); // overwrite, not accumulate
            TypeID element_type = arr->element_type();
            const Type* element_storage_type = storage_type(rt_, element_type);
            const StructDef* element_struct_def = rt_->get_struct_def(element_type);
            for (const auto& elem : jv) {
                if (element_storage_type && element_storage_type->type_kind == TK_struct) {
                    if (!elem.is_object() || !element_struct_def) {
                        ok = false;
                        continue;
                    }

                    HeapPtr ptr = rt_->heap_.allocate(
                        element_storage_type->size,
                        element_storage_type->alignment,
                        element_type);
                    auto* data = static_cast<uint8_t*>(rt_->heap_.get_ptr(ptr));
                    if (!data) {
                        ok = false;
                        continue;
                    }
                    rt_->initialize_zero_defaults(element_type, data);
                    Value value = Value::make_heap(ptr, element_type);
                    if (!struct_from_json(rt_, elem, value, element_struct_def)) {
                        ok = false;
                        continue;
                    }
                    arr->append_value(value, *rt_);
                    continue;
                }

                Value value;
                if (value_from_json(rt_, element_type, elem, value)) {
                    arr->append_value(value, *rt_);
                } else {
                    ok = false;
                }
            }
            continue;
        }

        if (fixed_array_type(rt_, fd.type_id)) {
            if (!fixed_array_from_json(rt_, jv, ref, fd)) {
                ok = false;
            }
            continue;
        }

        Value value;
        if (!value_from_json(rt_, fd.type_id, jv, value)) {
            ok = false;
            continue;
        }
        if (!rt_->write_value_field_at_offset(ref, fd.offset, fd.type_id, value)) {
            ok = false;
        }
    }
    return ok;
}

nlohmann::json JsonSerializer::serialize(const Value& ref,
    const StructDef* def) const
{
    return serialize_struct(ref, def);
}

bool JsonSerializer::deserialize(const nlohmann::json& j,
    const Value& ref, const StructDef* def) const
{
    return deserialize_struct(j, ref, def);
}

} // namespace nw::smalls
