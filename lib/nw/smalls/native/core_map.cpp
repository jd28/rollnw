#include "../stdlib.hpp"

namespace nw::smalls {

void register_core_map(Runtime& rt)
{
    if (rt.get_native_module("core.map")) {
        return;
    }

    FunctionMetadata has_key_meta;
    has_key_meta.name = "has_key";
    has_key_meta.return_type = rt.bool_type();
    has_key_meta.params.push_back(ParamMetadata{String("m"), rt.any_map_type()});
    has_key_meta.params.push_back(ParamMetadata{String("key"), rt.any_type()});

    FunctionMetadata keys_meta;
    keys_meta.name = "keys";
    keys_meta.return_type = rt.any_array_type();
    keys_meta.params.push_back(ParamMetadata{String("m"), rt.any_map_type()});

    FunctionMetadata values_meta;
    values_meta.name = "values";
    values_meta.return_type = rt.any_array_type();
    values_meta.params.push_back(ParamMetadata{String("m"), rt.any_map_type()});

    ModuleInterface iface;
    iface.module_path = "core.map";
    iface.functions.push_back(has_key_meta);
    iface.functions.push_back(keys_meta);
    iface.functions.push_back(values_meta);
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.map.has_key",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 2) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }

            const Type* mtype = rt->get_type(args[0].type_id);
            if (!mtype || mtype->type_kind != TK_map) {
                return Value{};
            }

            TypeID key_type = mtype->type_params[0].as<TypeID>();
            if (args[1].type_id != key_type) {
                return Value{};
            }

            return Value::make_bool(rt->map_contains(args[0].data.hptr, args[1]));
        },
        .metadata = has_key_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.map.keys",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }

            const Type* mtype = rt->get_type(args[0].type_id);
            if (!mtype || mtype->type_kind != TK_map) {
                return Value{};
            }

            TypeID key_type = mtype->type_params[0].as<TypeID>();

            HeapPtr out_ptr = rt->create_array_typed(key_type, rt->map_size(args[0].data.hptr));
            if (out_ptr.value == 0) {
                return Value{};
            }
            IArray* out = rt->get_array_typed(out_ptr);
            if (!out) {
                return Value{};
            }

            HeapPtr iter = rt->map_iter_begin(args[0].data.hptr);
            Value k;
            Value v;
            while (rt->map_iter_next(iter, k, v)) {
                out->append_value(k, *rt);
            }
            rt->map_iter_end(args[0].data.hptr, iter);

            return Value::make_heap(out_ptr, rt->heap_.get_header(out_ptr)->type_id);
        },
        .metadata = keys_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.map.values",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }

            const Type* mtype = rt->get_type(args[0].type_id);
            if (!mtype || mtype->type_kind != TK_map) {
                return Value{};
            }

            TypeID val_type = mtype->type_params[1].as<TypeID>();

            HeapPtr out_ptr = rt->create_array_typed(val_type, rt->map_size(args[0].data.hptr));
            if (out_ptr.value == 0) {
                return Value{};
            }
            IArray* out = rt->get_array_typed(out_ptr);
            if (!out) {
                return Value{};
            }

            HeapPtr iter = rt->map_iter_begin(args[0].data.hptr);
            Value k;
            Value v;
            while (rt->map_iter_next(iter, k, v)) {
                out->append_value(v, *rt);
            }
            rt->map_iter_end(args[0].data.hptr, iter);

            return Value::make_heap(out_ptr, rt->heap_.get_header(out_ptr)->type_id);
        },
        .metadata = values_meta});
}

} // namespace nw::smalls
