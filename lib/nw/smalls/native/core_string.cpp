#include "../runtime.hpp"

#include "../stdlib.hpp"

#include <cctype>

namespace nw::smalls {

void register_core_string(Runtime& rt)
{
    if (rt.get_native_module("core.string")) {
        return;
    }

    FunctionMetadata pad_left_meta;
    pad_left_meta.name = "pad_left";
    pad_left_meta.return_type = rt.string_type();
    pad_left_meta.params.push_back(ParamMetadata{String("s"), rt.string_type()});
    pad_left_meta.params.push_back(ParamMetadata{String("width"), rt.int_type()});
    pad_left_meta.params.push_back(ParamMetadata{String("pad_char"), rt.string_type()});

    FunctionMetadata pad_right_meta;
    pad_right_meta.name = "pad_right";
    pad_right_meta.return_type = rt.string_type();
    pad_right_meta.params.push_back(ParamMetadata{String("s"), rt.string_type()});
    pad_right_meta.params.push_back(ParamMetadata{String("width"), rt.int_type()});
    pad_right_meta.params.push_back(ParamMetadata{String("pad_char"), rt.string_type()});

    FunctionMetadata last_index_of_meta;
    last_index_of_meta.name = "last_index_of";
    last_index_of_meta.return_type = rt.int_type();
    last_index_of_meta.params.push_back(ParamMetadata{String("s"), rt.string_type()});
    last_index_of_meta.params.push_back(ParamMetadata{String("needle"), rt.string_type()});

    FunctionMetadata count_meta;
    count_meta.name = "count";
    count_meta.return_type = rt.int_type();
    count_meta.params.push_back(ParamMetadata{String("s"), rt.string_type()});
    count_meta.params.push_back(ParamMetadata{String("needle"), rt.string_type()});

    FunctionMetadata trim_start_meta;
    trim_start_meta.name = "trim_start";
    trim_start_meta.return_type = rt.string_type();
    trim_start_meta.params.push_back(ParamMetadata{String("s"), rt.string_type()});

    FunctionMetadata trim_end_meta;
    trim_end_meta.name = "trim_end";
    trim_end_meta.return_type = rt.string_type();
    trim_end_meta.params.push_back(ParamMetadata{String("s"), rt.string_type()});

    FunctionMetadata format_meta;
    format_meta.name = "format";
    format_meta.return_type = rt.string_type();
    format_meta.params.push_back(ParamMetadata{String("fmt"), rt.string_type()});
    format_meta.params.push_back(ParamMetadata{String("args"), rt.any_array_type()});

    ModuleInterface iface;
    iface.module_path = "core.string";
    iface.functions.push_back(pad_left_meta);
    iface.functions.push_back(pad_right_meta);
    iface.functions.push_back(last_index_of_meta);
    iface.functions.push_back(count_meta);
    iface.functions.push_back(trim_start_meta);
    iface.functions.push_back(trim_end_meta);
    iface.functions.push_back(format_meta);
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.string.pad_left",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 3 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type() || args[1].type_id != rt->int_type()
                || args[2].type_id != rt->string_type()) {
                return Value{};
            }
            String str;
            if (args[0].data.hptr.value != 0) {
                str = String(rt->get_string_view(args[0].data.hptr));
            }
            int32_t width = args[1].data.ival;
            StringView pad_str = " ";
            if (args[2].data.hptr.value != 0) {
                pad_str = rt->get_string_view(args[2].data.hptr);
            }
            if (width <= static_cast<int32_t>(str.size()) || pad_str.empty()) {
                return Value::make_string(rt->alloc_string(str));
            }
            size_t pad_count = static_cast<size_t>(width) - str.size();
            String result;
            result.reserve(static_cast<size_t>(width));
            for (size_t i = 0; i < pad_count; ++i) {
                result += pad_str[i % pad_str.size()];
            }
            result += str;
            return Value::make_string(rt->alloc_string(result));
        },
        .metadata = pad_left_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.string.pad_right",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 3 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type() || args[1].type_id != rt->int_type()
                || args[2].type_id != rt->string_type()) {
                return Value{};
            }
            String str;
            if (args[0].data.hptr.value != 0) {
                str = String(rt->get_string_view(args[0].data.hptr));
            }
            int32_t width = args[1].data.ival;
            StringView pad_str = " ";
            if (args[2].data.hptr.value != 0) {
                pad_str = rt->get_string_view(args[2].data.hptr);
            }
            if (width <= static_cast<int32_t>(str.size()) || pad_str.empty()) {
                return Value::make_string(rt->alloc_string(str));
            }
            size_t pad_count = static_cast<size_t>(width) - str.size();
            String result = str;
            result.reserve(static_cast<size_t>(width));
            for (size_t i = 0; i < pad_count; ++i) {
                result += pad_str[i % pad_str.size()];
            }
            return Value::make_string(rt->alloc_string(result));
        },
        .metadata = pad_right_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.string.last_index_of",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 2 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type() || args[1].type_id != rt->string_type()) {
                return Value{};
            }
            StringView haystack;
            if (args[0].data.hptr.value != 0) {
                haystack = rt->get_string_view(args[0].data.hptr);
            }
            StringView needle;
            if (args[1].data.hptr.value != 0) {
                needle = rt->get_string_view(args[1].data.hptr);
            }
            if (needle.empty()) {
                return Value::make_int(static_cast<int32_t>(haystack.size()));
            }
            size_t pos = haystack.rfind(needle);
            if (pos == StringView::npos) {
                return Value::make_int(-1);
            }
            return Value::make_int(static_cast<int32_t>(pos));
        },
        .metadata = last_index_of_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.string.count",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 2 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type() || args[1].type_id != rt->string_type()) {
                return Value{};
            }
            StringView haystack;
            if (args[0].data.hptr.value != 0) {
                haystack = rt->get_string_view(args[0].data.hptr);
            }
            StringView needle;
            if (args[1].data.hptr.value != 0) {
                needle = rt->get_string_view(args[1].data.hptr);
            }
            if (needle.empty()) {
                return Value::make_int(0);
            }
            int32_t count = 0;
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != StringView::npos) {
                ++count;
                pos += needle.size();
            }
            return Value::make_int(count);
        },
        .metadata = count_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.string.trim_start",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type()) {
                return Value{};
            }
            StringView str;
            if (args[0].data.hptr.value != 0) {
                str = rt->get_string_view(args[0].data.hptr);
            }
            size_t start = 0;
            while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
                ++start;
            }
            return Value::make_string(rt->alloc_string(str.substr(start)));
        },
        .metadata = trim_start_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.string.trim_end",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 1 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type()) {
                return Value{};
            }
            StringView str;
            if (args[0].data.hptr.value != 0) {
                str = rt->get_string_view(args[0].data.hptr);
            }
            size_t end = str.size();
            while (end > 0 && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
                --end;
            }
            return Value::make_string(rt->alloc_string(str.substr(0, end)));
        },
        .metadata = trim_end_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.string.format",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (argc != 2 || !rt) { return Value{}; }
            if (args[0].type_id != rt->string_type()) { return Value{}; }
            if (args[1].storage != ValueStorage::heap || args[1].data.hptr.value == 0) { return Value{}; }

            StringView fmt_sv;
            if (args[0].data.hptr.value != 0) {
                fmt_sv = rt->get_string_view(args[0].data.hptr);
            }

            IArray* arr = rt->get_array_typed(args[1].data.hptr);
            if (!arr || arr->element_type() != rt->string_type()) {
                return Value{};
            }

            String out;
            out.reserve(fmt_sv.size());

            size_t arg_idx = 0;
            size_t arg_count = arr->size();
            Value elem;

            for (size_t i = 0; i < fmt_sv.size(); ++i) {
                char ch = fmt_sv[i];
                if (ch == '{') {
                    if (i + 1 < fmt_sv.size() && fmt_sv[i + 1] == '{') {
                        out.push_back('{');
                        ++i;
                        continue;
                    }
                    if (i + 1 < fmt_sv.size() && fmt_sv[i + 1] == '}') {
                        if (arg_idx < arg_count && arr->get_value(arg_idx, elem, *rt)
                            && elem.type_id == rt->string_type() && elem.data.hptr.value != 0) {
                            out.append(rt->get_string_view(elem.data.hptr));
                        } else if (arg_idx < arg_count) {
                            // Present but empty/invalid element.
                        } else {
                            out.append("{}");
                        }
                        ++arg_idx;
                        ++i;
                        continue;
                    }
                } else if (ch == '}') {
                    if (i + 1 < fmt_sv.size() && fmt_sv[i + 1] == '}') {
                        out.push_back('}');
                        ++i;
                        continue;
                    }
                }

                out.push_back(ch);
            }

            return Value::make_string(rt->alloc_string(out));
        },
        .metadata = format_meta});
}

} // namespace nw::smalls
