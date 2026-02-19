#include <nw/kernel/Kernel.hpp>
#include <nw/rules/RuntimeObject.hpp>
#include <nw/smalls/UnmanagedArray.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>

namespace nwk = nw::kernel;
namespace nws = nw::smalls;

// == Micro-benchmarks for UnmanagedArray Operations ===========================

static void BM_unmanaged_array_alloc_free(benchmark::State& state)
{
    nw::RuntimeObjectPool pool;

    for (auto _ : state) {
        nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 4);
        benchmark::DoNotOptimize(h);
        pool.destroy_unmanaged_array(h);
    }
}
BENCHMARK(BM_unmanaged_array_alloc_free);

static void BM_unmanaged_array_push(benchmark::State& state)
{
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 4);
    nws::IArray* arr = pool.get_unmanaged_array(h);

    int32_t val = 0;
    for (auto _ : state) {
        nws::Value v = nws::Value::make_int(val++);
        arr->append_value(v, rt);
        if (arr->size() > 1000) {
            arr->clear();
        }
    }

    pool.destroy_unmanaged_array(h);
}
BENCHMARK(BM_unmanaged_array_push);

static void BM_unmanaged_array_get(benchmark::State& state)
{
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 100);
    nws::IArray* arr = pool.get_unmanaged_array(h);

    // Populate array
    for (int i = 0; i < 100; ++i) {
        arr->append_value(nws::Value::make_int(i), rt);
    }

    size_t idx = 0;
    for (auto _ : state) {
        nws::Value out;
        arr->get_value(idx % 100, out, rt);
        benchmark::DoNotOptimize(out);
        ++idx;
    }

    pool.destroy_unmanaged_array(h);
}
BENCHMARK(BM_unmanaged_array_get);

static void BM_unmanaged_array_set(benchmark::State& state)
{
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 100);
    nws::IArray* arr = pool.get_unmanaged_array(h);
    arr->resize(100);

    size_t idx = 0;
    int32_t val = 0;
    for (auto _ : state) {
        nws::Value v = nws::Value::make_int(val++);
        arr->set_value(idx % 100, v, rt);
        ++idx;
    }

    pool.destroy_unmanaged_array(h);
}
BENCHMARK(BM_unmanaged_array_set);

// == Comparison: UnmanagedArray vs ScriptHeap Array ===========================

static void BM_unmanaged_array_push_heavy(benchmark::State& state)
{
    const int num_pushes = state.range(0);
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    for (auto _ : state) {
        nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 0);
        nws::IArray* arr = pool.get_unmanaged_array(h);

        for (int i = 0; i < num_pushes; ++i) {
            arr->append_value(nws::Value::make_int(i), rt);
        }

        benchmark::DoNotOptimize(arr->size());
        pool.destroy_unmanaged_array(h);
    }

    state.SetItemsProcessed(state.iterations() * num_pushes);
}
BENCHMARK(BM_unmanaged_array_push_heavy)->Arg(10)->Arg(100)->Arg(1000);

static void BM_scriptheap_array_push_heavy(benchmark::State& state)
{
    const int num_pushes = state.range(0);
    nws::Runtime& rt = nwk::runtime();

    for (auto _ : state) {
        nws::HeapPtr ptr = rt.create_array_typed(rt.int_type(), 0);
        nws::IArray* arr = rt.get_array_typed(ptr);

        for (int i = 0; i < num_pushes; ++i) {
            arr->append_value(nws::Value::make_int(i), rt);
        }

        benchmark::DoNotOptimize(arr->size());
        // ScriptHeap arrays are GC-managed, no explicit cleanup
    }

    state.SetItemsProcessed(state.iterations() * num_pushes);

    // Cleanup any remaining arrays
    if (auto* gc = rt.gc()) {
        gc->collect_major();
    }
}
BENCHMARK(BM_scriptheap_array_push_heavy)->Arg(10)->Arg(100)->Arg(1000);

// == Stress Test: Many Small Arrays ==========================================

static void BM_unmanaged_array_many_small(benchmark::State& state)
{
    const int num_arrays = state.range(0);
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    std::vector<nw::TypedHandle> handles;
    handles.reserve(num_arrays);

    for (auto _ : state) {
        state.PauseTiming();
        handles.clear();

        // Allocate arrays
        for (int i = 0; i < num_arrays; ++i) {
            handles.push_back(pool.allocate_unmanaged_array(nws::TypeID{1}, 4));
        }
        state.ResumeTiming();

        // Push a few elements to each
        for (auto h : handles) {
            nws::IArray* arr = pool.get_unmanaged_array(h);
            arr->append_value(nws::Value::make_int(42), rt);
            benchmark::DoNotOptimize(arr->size());
        }

        state.PauseTiming();
        // Cleanup
        for (auto h : handles) {
            pool.destroy_unmanaged_array(h);
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * num_arrays);
}
BENCHMARK(BM_unmanaged_array_many_small)->Arg(10)->Arg(100)->Arg(1000);

// == Memory Layout Benchmark ==================================================

static void BM_unmanaged_array_dense_mutation(benchmark::State& state)
{
    const int num_arrays = state.range(0);
    const int mutations_per_iter = 100;
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    std::vector<nw::TypedHandle> handles;
    handles.reserve(num_arrays);

    // Setup: create arrays with initial data
    for (int i = 0; i < num_arrays; ++i) {
        nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 64);
        handles.push_back(h);
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> array_dist(0, num_arrays - 1);

    for (auto _ : state) {
        for (int m = 0; m < mutations_per_iter; ++m) {
            int arr_idx = array_dist(rng);
            nws::IArray* arr = pool.get_unmanaged_array(handles[arr_idx]);

            arr->append_value(nws::Value::make_int(m), rt);

            if (arr->size() > 64) {
                arr->clear();
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * mutations_per_iter);

    // Cleanup
    for (auto h : handles) {
        pool.destroy_unmanaged_array(h);
    }
}
BENCHMARK(BM_unmanaged_array_dense_mutation)->Arg(10)->Arg(100)->Arg(1000);

// == Comparison: GC Root Enumeration ==========================================

static void BM_gc_roots_with_unmanaged_arrays(benchmark::State& state)
{
    const int num_objects = state.range(0);
    auto& rt = nwk::runtime();
    auto* gc = rt.gc();

    if (!gc) {
        state.SkipWithError("GC unavailable");
        return;
    }

    nw::RuntimeObjectPool pool;

    // Create many unmanaged arrays (should NOT be GC-visible)
    std::vector<nw::TypedHandle> handles;
    handles.reserve(num_objects);
    for (int i = 0; i < num_objects; ++i) {
        handles.push_back(pool.allocate_unmanaged_array(nws::TypeID{1}, 16));
    }

    for (auto _ : state) {
        gc->collect_minor();
    }

    // Cleanup
    for (auto h : handles) {
        pool.destroy_unmanaged_array(h);
    }
}
BENCHMARK(BM_gc_roots_with_unmanaged_arrays)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_gc_roots_with_scriptheap_arrays(benchmark::State& state)
{
    const int num_objects = state.range(0);
    auto& rt = nwk::runtime();
    auto* gc = rt.gc();

    if (!gc) {
        state.SkipWithError("GC unavailable");
        return;
    }

    // Create ScriptHeap arrays (GC-visible)
    std::vector<nws::HeapPtr> ptrs;
    ptrs.reserve(num_objects);
    for (int i = 0; i < num_objects; ++i) {
        ptrs.push_back(rt.create_array_typed(rt.int_type(), 16));
    }

    for (auto _ : state) {
        gc->collect_minor();
    }

    // Cleanup via GC
    ptrs.clear();
    gc->collect_major();
}
BENCHMARK(BM_gc_roots_with_scriptheap_arrays)->Arg(100)->Arg(1000);

// == Throughput Benchmark =====================================================

static void BM_unmanaged_array_throughput(benchmark::State& state)
{
    nw::RuntimeObjectPool pool;
    nws::Runtime& rt = nwk::runtime();

    nw::TypedHandle h = pool.allocate_unmanaged_array(nws::TypeID{1}, 1024);
    nws::IArray* arr = pool.get_unmanaged_array(h);

    int32_t val = 0;
    for (auto _ : state) {
        // Mixed workload: 70% push, 20% get, 10% clear
        for (int i = 0; i < 100; ++i) {
            arr->append_value(nws::Value::make_int(val++), rt);
        }

        for (int i = 0; i < 20; ++i) {
            nws::Value out;
            arr->get_value(i * 5, out, rt);
        }

        if (arr->size() > 512) {
            arr->clear();
        }
    }

    pool.destroy_unmanaged_array(h);
}
BENCHMARK(BM_unmanaged_array_throughput);
