#include "../stdlib.hpp"

#include <cmath>

namespace nw::smalls {

namespace {

struct SortContext {
    IArray* arr;
    Runtime* rt;
    ScriptClosure* closure;
    Vector<Value>* call_args;
    bool failed = false;

    // Returns true if arr[a] < arr[b]
    bool less(size_t a, size_t b)
    {
        if (failed) { return false; }
        Value va, vb;
        if (!arr->get_value(a, va, *rt) || !arr->get_value(b, vb, *rt)) {
            failed = true;
            return false;
        }
        call_args->clear();
        call_args->push_back(va);
        call_args->push_back(vb);
        Value r = rt->call_closure(*closure, *call_args);
        if (r.type_id != rt->bool_type()) {
            failed = true;
            return false;
        }
        return r.data.bval;
    }

    void swap(size_t a, size_t b)
    {
        if (failed || a == b) { return; }
        Value va, vb;
        if (!arr->get_value(a, va, *rt) || !arr->get_value(b, vb, *rt)) {
            failed = true;
            return;
        }
        if (!arr->set_value(a, vb, *rt) || !arr->set_value(b, va, *rt)) {
            failed = true;
        }
    }
};

void insertion_sort(SortContext& ctx, size_t lo, size_t hi)
{
    for (size_t i = lo + 1; i < hi; ++i) {
        if (ctx.failed) { return; }
        Value key;
        if (!ctx.arr->get_value(i, key, *ctx.rt)) { ctx.failed = true; return; }

        size_t j = i;
        while (j > lo) {
            Value prev;
            if (!ctx.arr->get_value(j - 1, prev, *ctx.rt)) { ctx.failed = true; return; }

            ctx.call_args->clear();
            ctx.call_args->push_back(key);
            ctx.call_args->push_back(prev);
            Value r = ctx.rt->call_closure(*ctx.closure, *ctx.call_args);
            if (r.type_id != ctx.rt->bool_type()) { ctx.failed = true; return; }
            if (!r.data.bval) { break; }

            if (!ctx.arr->set_value(j, prev, *ctx.rt)) { ctx.failed = true; return; }
            --j;
        }
        if (!ctx.arr->set_value(j, key, *ctx.rt)) { ctx.failed = true; return; }
    }
}

void sift_down(SortContext& ctx, size_t lo, size_t node, size_t hi)
{
    while (true) {
        if (ctx.failed) { return; }
        size_t child = 2 * (node - lo) + 1 + lo;
        if (child >= hi) { break; }
        if (child + 1 < hi && ctx.less(child, child + 1)) {
            ++child;
        }
        if (ctx.failed) { return; }
        if (!ctx.less(node, child)) { break; }
        ctx.swap(node, child);
        node = child;
    }
}

void heapsort(SortContext& ctx, size_t lo, size_t hi)
{
    size_t n = hi - lo;
    if (n < 2) { return; }

    for (size_t i = lo + n / 2; i > lo; --i) {
        sift_down(ctx, lo, i - 1, hi);
        if (ctx.failed) { return; }
    }
    sift_down(ctx, lo, lo, hi);

    for (size_t i = hi - 1; i > lo; --i) {
        if (ctx.failed) { return; }
        ctx.swap(lo, i);
        sift_down(ctx, lo, lo, i);
    }
}

size_t median_of_three(SortContext& ctx, size_t a, size_t b, size_t c)
{
    if (ctx.less(a, b)) {
        if (ctx.less(b, c)) { return b; }
        return ctx.less(a, c) ? c : a;
    }
    if (ctx.less(a, c)) { return a; }
    return ctx.less(b, c) ? c : b;
}

size_t partition(SortContext& ctx, size_t lo, size_t hi)
{
    size_t mid = lo + (hi - lo) / 2;
    size_t pivot = median_of_three(ctx, lo, mid, hi - 1);
    if (ctx.failed) { return lo; }

    ctx.swap(pivot, hi - 1);
    size_t store = lo;
    for (size_t i = lo; i < hi - 1; ++i) {
        if (ctx.failed) { return lo; }
        if (ctx.less(i, hi - 1)) {
            ctx.swap(i, store);
            ++store;
        }
    }
    ctx.swap(store, hi - 1);
    return store;
}

void introsort_impl(SortContext& ctx, size_t lo, size_t hi, int depth_limit)
{
    while (hi - lo > 16) {
        if (ctx.failed) { return; }
        if (depth_limit == 0) {
            heapsort(ctx, lo, hi);
            return;
        }
        --depth_limit;
        size_t p = partition(ctx, lo, hi);
        if (ctx.failed) { return; }
        if (p - lo < hi - (p + 1)) {
            introsort_impl(ctx, lo, p, depth_limit);
            lo = p + 1;
        } else {
            introsort_impl(ctx, p + 1, hi, depth_limit);
            hi = p;
        }
    }
    insertion_sort(ctx, lo, hi);
}

void introsort(SortContext& ctx, size_t n)
{
    if (n < 2) { return; }
    int depth_limit = static_cast<int>(2.0 * std::log2(static_cast<double>(n)));
    introsort_impl(ctx, 0, n, depth_limit);
}

} // anonymous namespace

void register_core_array(Runtime& rt)
{
    if (rt.get_native_module("core.array")) {
        return;
    }

    FunctionMetadata map_meta;
    map_meta.name = "map";
    map_meta.return_type = rt.any_array_type();
    map_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});
    map_meta.params.push_back(ParamMetadata{String("f"), rt.any_type()});

    FunctionMetadata filter_meta;
    filter_meta.name = "filter";
    filter_meta.return_type = rt.any_array_type();
    filter_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});
    filter_meta.params.push_back(ParamMetadata{String("pred"), rt.any_type()});

    FunctionMetadata reduce_meta;
    reduce_meta.name = "reduce";
    reduce_meta.return_type = rt.any_type();
    reduce_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});
    reduce_meta.params.push_back(ParamMetadata{String("init"), rt.any_type()});
    reduce_meta.params.push_back(ParamMetadata{String("step"), rt.any_type()});

    FunctionMetadata sort_meta;
    sort_meta.name = "sort";
    sort_meta.return_type = rt.void_type();
    sort_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});
    sort_meta.params.push_back(ParamMetadata{String("less"), rt.any_type()});

    FunctionMetadata find_meta;
    find_meta.name = "find";
    find_meta.return_type = rt.int_type();
    find_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});
    find_meta.params.push_back(ParamMetadata{String("pred"), rt.any_type()});

    FunctionMetadata reverse_meta;
    reverse_meta.name = "reverse";
    reverse_meta.return_type = rt.void_type();
    reverse_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});

    FunctionMetadata slice_meta;
    slice_meta.name = "slice";
    slice_meta.return_type = rt.any_array_type();
    slice_meta.params.push_back(ParamMetadata{String("a"), rt.any_array_type()});
    slice_meta.params.push_back(ParamMetadata{String("start"), rt.int_type()});
    slice_meta.params.push_back(ParamMetadata{String("end"), rt.int_type()});

    ModuleInterface iface;
    iface.module_path = "core.array";
    iface.functions.push_back(map_meta);
    iface.functions.push_back(filter_meta);
    iface.functions.push_back(reduce_meta);
    iface.functions.push_back(sort_meta);
    iface.functions.push_back(find_meta);
    iface.functions.push_back(reverse_meta);
    iface.functions.push_back(slice_meta);
    rt.register_native_interface(std::move(iface));

    rt.register_native_function(NativeFunction{
        .name = "core.array.map",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 2) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }
            if (args[1].storage != ValueStorage::heap || args[1].data.hptr.value == 0) { return Value{}; }

            IArray* in = rt->get_array_typed(args[0].data.hptr);
            if (!in) { return Value{}; }
            TypeID in_elem = in->element_type();

            const Type* ftype = rt->get_type(args[1].type_id);
            if (!ftype || ftype->type_kind != TK_function) { return Value{}; }
            if (rt->get_function_param_count(args[1].type_id) != 1) { return Value{}; }
            TypeID param0 = rt->get_function_param_type(args[1].type_id, 0);
            if (param0 != rt->any_type() && param0 != in_elem) { return Value{}; }

            TypeID out_elem = rt->get_function_return_type(args[1].type_id);
            if (out_elem == invalid_type_id || out_elem == rt->any_type()) { return Value{}; }

            HeapPtr out_ptr = rt->create_array_typed(out_elem, in->size());
            if (out_ptr.value == 0) { return Value{}; }

            IArray* out = rt->get_array_typed(out_ptr);
            if (!out) { return Value{}; }

            ScriptClosure closure{args[1].data.hptr};
            Vector<Value> call_args;
            call_args.reserve(1);

            Value v;
            for (size_t i = 0; i < in->size(); ++i) {
                if (!in->get_value(i, v, *rt)) {
                    return Value{};
                }
                call_args.clear();
                call_args.push_back(v);
                Value r = rt->call_closure(closure, call_args);
                if (r.type_id != out_elem) {
                    return Value{};
                }
                out->append_value(r, *rt);
            }

            return Value::make_heap(out_ptr, rt->heap_.get_header(out_ptr)->type_id);
        },
        .metadata = map_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.array.filter",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 2) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }
            if (args[1].storage != ValueStorage::heap || args[1].data.hptr.value == 0) { return Value{}; }

            IArray* in = rt->get_array_typed(args[0].data.hptr);
            if (!in) { return Value{}; }
            TypeID elem_type = in->element_type();

            const Type* ptype = rt->get_type(args[1].type_id);
            if (!ptype || ptype->type_kind != TK_function) { return Value{}; }
            if (rt->get_function_param_count(args[1].type_id) != 1) { return Value{}; }
            TypeID param0 = rt->get_function_param_type(args[1].type_id, 0);
            if (param0 != rt->any_type() && param0 != elem_type) { return Value{}; }
            if (rt->get_function_return_type(args[1].type_id) != rt->bool_type()) { return Value{}; }

            HeapPtr out_ptr = rt->create_array_typed(elem_type, 0);
            if (out_ptr.value == 0) { return Value{}; }
            IArray* out = rt->get_array_typed(out_ptr);
            if (!out) { return Value{}; }

            ScriptClosure closure{args[1].data.hptr};
            Vector<Value> call_args;
            call_args.reserve(1);

            Value v;
            for (size_t i = 0; i < in->size(); ++i) {
                if (!in->get_value(i, v, *rt)) {
                    return Value{};
                }
                call_args.clear();
                call_args.push_back(v);
                Value r = rt->call_closure(closure, call_args);
                if (r.type_id != rt->bool_type()) {
                    return Value{};
                }
                if (r.data.bval) {
                    out->append_value(v, *rt);
                }
            }

            return Value::make_heap(out_ptr, rt->heap_.get_header(out_ptr)->type_id);
        },
        .metadata = filter_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.array.reduce",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 3) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }
            if (args[2].storage != ValueStorage::heap || args[2].data.hptr.value == 0) { return Value{}; }

            IArray* in = rt->get_array_typed(args[0].data.hptr);
            if (!in) { return Value{}; }

            TypeID elem_type = in->element_type();
            TypeID acc_type = args[1].type_id;
            if (acc_type == invalid_type_id || acc_type == rt->any_type()) { return Value{}; }

            const Type* stype = rt->get_type(args[2].type_id);
            if (!stype || stype->type_kind != TK_function) { return Value{}; }
            if (rt->get_function_param_count(args[2].type_id) != 2) { return Value{}; }

            TypeID p0 = rt->get_function_param_type(args[2].type_id, 0);
            TypeID p1 = rt->get_function_param_type(args[2].type_id, 1);
            if (p0 != rt->any_type() && p0 != acc_type) { return Value{}; }
            if (p1 != rt->any_type() && p1 != elem_type) { return Value{}; }
            if (rt->get_function_return_type(args[2].type_id) != acc_type) { return Value{}; }

            Value acc = args[1];
            ScriptClosure closure{args[2].data.hptr};
            Vector<Value> call_args;
            call_args.reserve(2);

            Value v;
            for (size_t i = 0; i < in->size(); ++i) {
                if (!in->get_value(i, v, *rt)) {
                    return Value{};
                }
                call_args.clear();
                call_args.push_back(acc);
                call_args.push_back(v);
                Value r = rt->call_closure(closure, call_args);
                if (r.type_id != acc_type) {
                    return Value{};
                }
                acc = r;
            }

            return acc;
        },
        .metadata = reduce_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.array.sort",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 2) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }
            if (args[1].storage != ValueStorage::heap || args[1].data.hptr.value == 0) { return Value{}; }

            IArray* arr = rt->get_array_typed(args[0].data.hptr);
            if (!arr) { return Value{}; }
            TypeID elem_type = arr->element_type();

            const Type* ltype = rt->get_type(args[1].type_id);
            if (!ltype || ltype->type_kind != TK_function) { return Value{}; }
            if (rt->get_function_param_count(args[1].type_id) != 2) { return Value{}; }
            TypeID p0 = rt->get_function_param_type(args[1].type_id, 0);
            TypeID p1 = rt->get_function_param_type(args[1].type_id, 1);
            if (p0 != rt->any_type() && p0 != elem_type) { return Value{}; }
            if (p1 != rt->any_type() && p1 != elem_type) { return Value{}; }
            if (rt->get_function_return_type(args[1].type_id) != rt->bool_type()) { return Value{}; }

            ScriptClosure closure{args[1].data.hptr};
            Vector<Value> call_args;
            call_args.reserve(2);

            SortContext ctx{arr, rt, &closure, &call_args};
            introsort(ctx, arr->size());
            if (ctx.failed) { return Value{}; }

            return Value(rt->void_type());
        },
        .metadata = sort_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.array.find",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 2) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }
            if (args[1].storage != ValueStorage::heap || args[1].data.hptr.value == 0) { return Value{}; }

            IArray* in = rt->get_array_typed(args[0].data.hptr);
            if (!in) { return Value{}; }
            TypeID elem_type = in->element_type();

            const Type* ptype = rt->get_type(args[1].type_id);
            if (!ptype || ptype->type_kind != TK_function) { return Value{}; }
            if (rt->get_function_param_count(args[1].type_id) != 1) { return Value{}; }
            TypeID param0 = rt->get_function_param_type(args[1].type_id, 0);
            if (param0 != rt->any_type() && param0 != elem_type) { return Value{}; }
            if (rt->get_function_return_type(args[1].type_id) != rt->bool_type()) { return Value{}; }

            ScriptClosure closure{args[1].data.hptr};
            Vector<Value> call_args;
            call_args.reserve(1);

            Value v;
            for (size_t i = 0; i < in->size(); ++i) {
                if (!in->get_value(i, v, *rt)) { return Value{}; }
                call_args.clear();
                call_args.push_back(v);
                Value r = rt->call_closure(closure, call_args);
                if (r.type_id != rt->bool_type()) { return Value{}; }
                if (r.data.bval) {
                    return Value::make_int(static_cast<int32_t>(i));
                }
            }
            return Value::make_int(-1);
        },
        .metadata = find_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.array.reverse",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 1) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }

            IArray* arr = rt->get_array_typed(args[0].data.hptr);
            if (!arr) { return Value{}; }

            size_t n = arr->size();
            Value lo_val, hi_val;
            for (size_t i = 0; i < n / 2; ++i) {
                size_t j = n - 1 - i;
                if (!arr->get_value(i, lo_val, *rt)) { return Value{}; }
                if (!arr->get_value(j, hi_val, *rt)) { return Value{}; }
                if (!arr->set_value(i, hi_val, *rt)) { return Value{}; }
                if (!arr->set_value(j, lo_val, *rt)) { return Value{}; }
            }
            return Value(rt->void_type());
        },
        .metadata = reverse_meta});

    rt.register_native_function(NativeFunction{
        .name = "core.array.slice",
        .wrapper = [](Runtime* rt, const Value* args, uint8_t argc) -> Value {
            if (!rt || argc != 3) { return Value{}; }
            if (args[0].storage != ValueStorage::heap || args[0].data.hptr.value == 0) { return Value{}; }
            if (args[1].type_id != rt->int_type()) { return Value{}; }
            if (args[2].type_id != rt->int_type()) { return Value{}; }

            IArray* in = rt->get_array_typed(args[0].data.hptr);
            if (!in) { return Value{}; }
            TypeID elem_type = in->element_type();

            int32_t start = args[1].data.ival;
            int32_t end = args[2].data.ival;
            int32_t len = static_cast<int32_t>(in->size());

            if (start < 0) { start = std::max(0, len + start); }
            if (end < 0) { end = std::max(0, len + end); }
            if (start > len) { start = len; }
            if (end > len) { end = len; }
            if (start >= end) {
                HeapPtr out_ptr = rt->create_array_typed(elem_type, 0);
                if (out_ptr.value == 0) { return Value{}; }
                return Value::make_heap(out_ptr, rt->heap_.get_header(out_ptr)->type_id);
            }

            size_t count = static_cast<size_t>(end - start);
            HeapPtr out_ptr = rt->create_array_typed(elem_type, count);
            if (out_ptr.value == 0) { return Value{}; }
            IArray* out = rt->get_array_typed(out_ptr);
            if (!out) { return Value{}; }

            Value v;
            for (size_t i = 0; i < count; ++i) {
                if (!in->get_value(static_cast<size_t>(start) + i, v, *rt)) { return Value{}; }
                out->append_value(v, *rt);
            }

            return Value::make_heap(out_ptr, rt->heap_.get_header(out_ptr)->type_id);
        },
        .metadata = slice_meta});
}

} // namespace nw::smalls
