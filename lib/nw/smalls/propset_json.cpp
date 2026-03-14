#include "propset_json.hpp"

#include "Array.hpp"
#include "runtime.hpp"

#include "../log.hpp"
#include "../util/HandlePool.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <string>

namespace nw::smalls {

PropsetJsonSerializer::PropsetJsonSerializer(Runtime* rt)
    : rt_(rt)
{
}

// ---------------------------------------------------------------------------
// Serialize
// ---------------------------------------------------------------------------

nlohmann::json PropsetJsonSerializer::serialize(const Value& ref,
    const StructDef* def) const
{
    nlohmann::json j = nlohmann::json::object();
    if (!def) { return j; }

    for (uint32_t i = 0; i < def->field_count; ++i) {
        const FieldDef& fd = def->fields[i];
        const std::string key{fd.name.view()};
        const Type* ftype = rt_->get_type(fd.type_id);
        if (!ftype) { continue; }

        if (fd.is_unmanaged_array) {
            // TK_array (unmanaged propset array) → JSON array of ints
            Value arr_val = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
            TypedHandle h = TypedHandle::from_ull(arr_val.data.handle);
            IArray* arr = rt_->object_pool().get_unmanaged_array(h);
            if (!arr) {
                j[key] = nlohmann::json::array();
                continue;
            }
            nlohmann::json ja = nlohmann::json::array();
            for (size_t k = 0; k < arr->size(); ++k) {
                Value v;
                if (arr->get_value(k, v, *rt_)) {
                    ja.push_back(v.data.ival);
                }
            }
            j[key] = std::move(ja);
            continue;
        }

        if (ftype->type_kind == TK_primitive) {
            switch (ftype->primitive_kind) {
            case PK_int: {
                Value v = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
                j[key] = v.data.ival;
                break;
            }
            case PK_float: {
                Value v = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
                j[key] = v.data.fval;
                break;
            }
            case PK_bool: {
                Value v = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
                j[key] = v.data.bval;
                break;
            }
            default:
                // PK_string: heap ref — skip with warning (V2 scope)
                LOG_F(WARNING, "propset_json: skipping heap string field '{}'", key);
                j[key] = nullptr;
                break;
            }
            continue;
        }

        if (ftype->type_kind == TK_fixed_array) {
            // Inline int[N] — read N consecutive int32 values
            const Type* elem_type = rt_->get_type(ftype->type_params[0].as<TypeID>());
            int32_t count = ftype->type_params[1].as<int32_t>();
            if (elem_type && elem_type->type_kind == TK_primitive && elem_type->primitive_kind == PK_int) {
                nlohmann::json ja = nlohmann::json::array();
                for (int32_t k = 0; k < count; ++k) {
                    Value v = rt_->read_value_field_at_offset(ref,
                        fd.offset + uint32_t(k) * 4u, rt_->int_type());
                    ja.push_back(v.data.ival);
                }
                j[key] = std::move(ja);
            } else {
                j[key] = nullptr; // unsupported element type
            }
            continue;
        }

        // Object handles — TK_opaque immediate values; round-trip as uint64.
        if (rt_->is_object_like_type(fd.type_id)) {
            Value v = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
            j[key] = v.data.oval.to_ull();
            continue;
        }

        // Everything else (struct, map, closures) — skip with warning.
        LOG_F(WARNING, "propset_json: unsupported field type for '{}'", key);
        j[key] = nullptr;
    }
    return j;
}

// ---------------------------------------------------------------------------
// Deserialize
// ---------------------------------------------------------------------------

bool PropsetJsonSerializer::deserialize(const nlohmann::json& j,
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

        const Type* ftype = rt_->get_type(fd.type_id);
        if (!ftype) { ok = false; continue; }

        if (fd.is_unmanaged_array) {
            if (!jv.is_array()) { ok = false; continue; }
            Value arr_val = rt_->read_value_field_at_offset(ref, fd.offset, fd.type_id);
            TypedHandle h = TypedHandle::from_ull(arr_val.data.handle);
            IArray* arr = rt_->object_pool().get_unmanaged_array(h);
            if (!arr) { ok = false; continue; }
            arr->clear(); // overwrite, not accumulate
            for (const auto& elem : jv) {
                if (elem.is_number_integer()) {
                    arr->append_value(Value::make_int(elem.get<int32_t>()), *rt_);
                }
            }
            continue;
        }

        if (ftype->type_kind == TK_primitive) {
            switch (ftype->primitive_kind) {
            case PK_int:
                if (jv.is_number_integer()) {
                    rt_->write_value_field_at_offset(ref, fd.offset, fd.type_id,
                        Value::make_int(jv.get<int32_t>()));
                } else { ok = false; }
                break;
            case PK_float:
                if (jv.is_number()) {
                    rt_->write_value_field_at_offset(ref, fd.offset, fd.type_id,
                        Value::make_float(jv.get<float>()));
                } else { ok = false; }
                break;
            case PK_bool:
                if (jv.is_boolean()) {
                    rt_->write_value_field_at_offset(ref, fd.offset, fd.type_id,
                        Value::make_bool(jv.get<bool>()));
                } else { ok = false; }
                break;
            default:
                // String: heap ref — skip (V2)
                break;
            }
            continue;
        }

        if (ftype->type_kind == TK_fixed_array) {
            const Type* elem_type = rt_->get_type(ftype->type_params[0].as<TypeID>());
            int32_t count = ftype->type_params[1].as<int32_t>();
            if (jv.is_array() && elem_type
                && elem_type->type_kind == TK_primitive
                && elem_type->primitive_kind == PK_int)
            {
                int32_t n = std::min(count, static_cast<int32_t>(jv.size()));
                for (int32_t k = 0; k < n; ++k) {
                    if (jv[k].is_number_integer()) {
                        rt_->write_value_field_at_offset(ref,
                            fd.offset + uint32_t(k) * 4u, rt_->int_type(),
                            Value::make_int(jv[k].get<int32_t>()));
                    }
                }
            } else { ok = false; }
            continue;
        }

        // Object handles — restore from uint64.
        if (rt_->is_object_like_type(fd.type_id)) {
            if (jv.is_number_unsigned() || jv.is_number_integer()) {
                uint64_t raw = jv.get<uint64_t>();
                nw::ObjectHandle oh;
                static_assert(sizeof(oh) == sizeof(raw));
                std::memcpy(&oh, &raw, sizeof(raw));
                rt_->write_value_field_at_offset(ref, fd.offset, fd.type_id,
                    Value::make_object(oh));
            } else { ok = false; }
            continue;
        }

        // Other types: skip
    }
    return ok;
}

} // namespace nw::smalls
