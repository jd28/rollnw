#include "../stdlib.hpp"

#include <cmath>

namespace nw::smalls {

void register_core_math(Runtime& rt)
{
    if (rt.get_native_module("core.math")) {
        return;
    }

    // 1-param float -> float
    FunctionMetadata sin_meta;
    sin_meta.name = "sin";
    sin_meta.return_type = rt.float_type();
    sin_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata cos_meta;
    cos_meta.name = "cos";
    cos_meta.return_type = rt.float_type();
    cos_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata sqrt_meta;
    sqrt_meta.name = "sqrt";
    sqrt_meta.return_type = rt.float_type();
    sqrt_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata log_meta;
    log_meta.name = "log";
    log_meta.return_type = rt.float_type();
    log_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata exp_meta;
    exp_meta.name = "exp";
    exp_meta.return_type = rt.float_type();
    exp_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata ceil_meta;
    ceil_meta.name = "ceil";
    ceil_meta.return_type = rt.float_type();
    ceil_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata floor_meta;
    floor_meta.name = "floor";
    floor_meta.return_type = rt.float_type();
    floor_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    FunctionMetadata round_meta;
    round_meta.name = "round";
    round_meta.return_type = rt.float_type();
    round_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    // 2-param float -> float
    FunctionMetadata pow_meta;
    pow_meta.name = "pow";
    pow_meta.return_type = rt.float_type();
    pow_meta.params.push_back(ParamMetadata{String("base"), rt.float_type()});
    pow_meta.params.push_back(ParamMetadata{String("exponent"), rt.float_type()});

    FunctionMetadata atan2_meta;
    atan2_meta.name = "atan2";
    atan2_meta.return_type = rt.float_type();
    atan2_meta.params.push_back(ParamMetadata{String("y"), rt.float_type()});
    atan2_meta.params.push_back(ParamMetadata{String("x"), rt.float_type()});

    ModuleInterface iface;
    iface.module_path = "core.math";
    iface.functions.push_back(sin_meta);
    iface.functions.push_back(cos_meta);
    iface.functions.push_back(sqrt_meta);
    iface.functions.push_back(log_meta);
    iface.functions.push_back(exp_meta);
    iface.functions.push_back(ceil_meta);
    iface.functions.push_back(floor_meta);
    iface.functions.push_back(round_meta);
    iface.functions.push_back(pow_meta);
    iface.functions.push_back(atan2_meta);
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.math.sin",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::sin(args[0].data.fval)));
        },
        .metadata = sin_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.cos",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::cos(args[0].data.fval)));
        },
        .metadata = cos_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.sqrt",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::sqrt(args[0].data.fval)));
        },
        .metadata = sqrt_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.log",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::log(args[0].data.fval)));
        },
        .metadata = log_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.exp",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::exp(args[0].data.fval)));
        },
        .metadata = exp_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.ceil",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::ceil(args[0].data.fval)));
        },
        .metadata = ceil_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.floor",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::floor(args[0].data.fval)));
        },
        .metadata = floor_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.round",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::round(args[0].data.fval)));
        },
        .metadata = round_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.pow",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 2 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            if (args[1].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::pow(args[0].data.fval, args[1].data.fval)));
        },
        .metadata = pow_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.math.atan2",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 2 || !rt) { return Value{}; }
            if (args[0].type_id != rt->float_type()) { return Value{}; }
            if (args[1].type_id != rt->float_type()) { return Value{}; }
            return Value::make_float(static_cast<float>(std::atan2(args[0].data.fval, args[1].data.fval)));
        },
        .metadata = atan2_meta});

}

} // namespace nw::smalls
