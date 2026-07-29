#include "../stdlib.hpp"

#include "../stdlib.hpp"

namespace nw::smalls {

void register_core_test(Runtime& rt)
{
    if (rt.get_native_module("core.test")) {
        return;
    }

    FunctionMetadata test_meta;
    test_meta.name = "test";
    test_meta.return_type = rt.void_type();
    test_meta.params.push_back(ParamMetadata{String("name"), rt.string_type()});
    test_meta.params.push_back(ParamMetadata{String("passed"), rt.bool_type()});

    FunctionMetadata reset_meta;
    reset_meta.name = "reset";
    reset_meta.return_type = rt.void_type();

    FunctionMetadata summary_meta;
    summary_meta.name = "summary";
    summary_meta.return_type = rt.void_type();

    FunctionMetadata failures_meta;
    failures_meta.name = "failures";
    failures_meta.return_type = rt.int_type();

    FunctionMetadata count_meta;
    count_meta.name = "count";
    count_meta.return_type = rt.int_type();

    ModuleInterface iface;
    iface.module_path = "core.test";
    iface.functions.push_back(test_meta);
    iface.functions.push_back(reset_meta);
    iface.functions.push_back(summary_meta);
    iface.functions.push_back(failures_meta);
    iface.functions.push_back(count_meta);
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.test.test",
        .wrapper = +[](Runtime* runtime, const Value* args, uint8_t argc) -> Value {
            if (!runtime || argc != 2) { return Value{}; }
            if (args[0].type_id != runtime->string_type() || args[1].type_id != runtime->bool_type()) {
                return Value{};
            }

            StringView name = "<test>";
            if (args[0].data.hptr.value != 0) {
                name = runtime->get_string_view(args[0].data.hptr);
            }

            runtime->record_test_result(name, args[1].data.bval);
            return Value(runtime->void_type());
        },
        .metadata = test_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.test.reset",
        .wrapper = +[](Runtime* runtime, const Value*, uint8_t argc) -> Value {
            if (!runtime || argc != 0) { return Value{}; }
            runtime->reset_test_state();
            return Value(runtime->void_type());
        },
        .metadata = reset_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.test.summary",
        .wrapper = +[](Runtime* runtime, const Value*, uint8_t argc) -> Value {
            if (!runtime || argc != 0) { return Value{}; }
            runtime->report_test_summary();
            return Value(runtime->void_type());
        },
        .metadata = summary_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.test.failures",
        .wrapper = +[](Runtime* runtime, const Value*, uint8_t argc) -> Value {
            if (!runtime || argc != 0) { return Value{}; }
            return Value::make_int(static_cast<int32_t>(runtime->test_failures()));
        },
        .metadata = failures_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.test.count",
        .wrapper = +[](Runtime* runtime, const Value*, uint8_t argc) -> Value {
            if (!runtime || argc != 0) { return Value{}; }
            return Value::make_int(static_cast<int32_t>(runtime->test_count()));
        },
        .metadata = count_meta});
}

} // namespace nw::smalls
