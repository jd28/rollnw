#include "../stdlib.hpp"

#include "../../log.hpp"

namespace nw::smalls {

void register_core_prelude(Runtime& rt)
{
    if (rt.get_native_module("core.prelude")) {
        return;
    }

    FunctionMetadata print_meta;
    print_meta.name = "print";
    print_meta.return_type = rt.void_type();
    print_meta.params.push_back(ParamMetadata{String("message"), rt.string_type()});

    FunctionMetadata println_meta;
    println_meta.name = "println";
    println_meta.return_type = rt.void_type();
    println_meta.params.push_back(ParamMetadata{String("message"), rt.string_type()});

    FunctionMetadata assert_meta;
    assert_meta.name = "assert";
    assert_meta.return_type = rt.void_type();
    assert_meta.params.push_back(ParamMetadata{String("cond"), rt.bool_type()});

    FunctionMetadata error_meta;
    error_meta.name = "error";
    error_meta.return_type = rt.void_type();
    error_meta.params.push_back(ParamMetadata{String("message"), rt.string_type()});

    FunctionMetadata panic_meta;
    panic_meta.name = "panic";
    panic_meta.return_type = rt.void_type();
    panic_meta.params.push_back(ParamMetadata{String("message"), rt.string_type()});

    FunctionMetadata gc_collect_meta;
    gc_collect_meta.name = "gc_collect";
    gc_collect_meta.return_type = rt.void_type();

    ModuleInterface iface;
    iface.module_path = "core.prelude";
    iface.functions.push_back(print_meta);
    iface.functions.push_back(println_meta);
    iface.functions.push_back(assert_meta);
    iface.functions.push_back(error_meta);
    iface.functions.push_back(panic_meta);
    iface.functions.push_back(gc_collect_meta);
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.prelude.print",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].type_id != rt->string_type()) { return Value{}; }

            StringView msg;
            if (args[0].data.hptr.value != 0) {
                msg = rt->get_string_view(args[0].data.hptr);
            }
            LOG_F(INFO, "[smalls] {}", msg);
            return Value(rt->void_type());
        },
        .metadata = print_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.prelude.println",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].type_id != rt->string_type()) { return Value{}; }

            StringView msg;
            if (args[0].data.hptr.value != 0) {
                msg = rt->get_string_view(args[0].data.hptr);
            }
            LOG_F(INFO, "[smalls] {}", msg);
            return Value(rt->void_type());
        },
        .metadata = println_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.prelude.assert",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].type_id != rt->bool_type()) { return Value{}; }

            if (!args[0].data.bval) {
                rt->fail("assert failed");
            }
            return Value(rt->void_type());
        },
        .metadata = assert_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.prelude.error",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].type_id != rt->string_type()) { return Value{}; }

            StringView msg = "error";
            if (args[0].data.hptr.value != 0) {
                msg = rt->get_string_view(args[0].data.hptr);
            }
            rt->fail(msg);
            return Value(rt->void_type());
        },
        .metadata = error_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.prelude.panic",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].type_id != rt->string_type()) { return Value{}; }

            StringView msg = "panic";
            if (args[0].data.hptr.value != 0) {
                msg = rt->get_string_view(args[0].data.hptr);
            }
            rt->fail(msg);
            return Value(rt->void_type());
        },
        .metadata = panic_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.prelude.gc_collect",
        .wrapper = [](Runtime* rt, const Value*, uint8_t) -> Value {
            if (!rt) { return Value{}; }
            if (auto* gc = rt->gc()) {
                gc->collect_minor();
            }
            return Value(rt->void_type());
        },
        .metadata = gc_collect_meta});
}

} // namespace nw::smalls
