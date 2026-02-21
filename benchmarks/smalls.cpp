#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Memory.hpp>
#include <nw/smalls/AstCompiler.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/VirtualMachine.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

constexpr const char* k_smalls_source = R"(
fn main(): int {
    var arr: array!(int) = {1,2,3,4,5,6,7,8,9,10};
    var m: map!(int, int) = {1: 10, 2: 20, 3: 30, 4: 40};

    var sum = 0;
    var i = 0;
    for (i < 10) {
        sum = sum + arr[i];
        i = i + 1;
    }

    var j = 1;
    for (j <= 4) {
        sum = sum + m[j];
        j = j + 1;
    }

    return sum;
}
)";

} // namespace

static void BM_smalls_parse(benchmark::State& state)
{
    nw::MemoryArena arena(nw::MB(64));
    for (auto _ : state) {
        nw::MemoryScope scope(&arena);
        nw::smalls::Context ctx;
        ctx.arena = &arena;
        ctx.scope = &scope;

        nw::smalls::Script script("bench.smalls", k_smalls_source, &ctx);
        script.parse();
        benchmark::DoNotOptimize(script.errors());
    }
}
BENCHMARK(BM_smalls_parse);

static void BM_smalls_resolve(benchmark::State& state)
{
    nw::MemoryArena arena(nw::MB(128));
    for (auto _ : state) {
        nw::MemoryScope scope(&arena);
        nw::smalls::Context ctx;
        ctx.arena = &arena;
        ctx.scope = &scope;

        nw::smalls::Script script("bench.smalls", k_smalls_source, &ctx);
        script.parse();
        script.resolve();
        benchmark::DoNotOptimize(script.errors());
    }
}
BENCHMARK(BM_smalls_resolve);

static void BM_smalls_compile(benchmark::State& state)
{
    nw::MemoryArena arena(nw::MB(128));
    for (auto _ : state) {
        nw::MemoryScope scope(&arena);
        nw::smalls::Context ctx;
        ctx.arena = &arena;
        ctx.scope = &scope;

        nw::smalls::Script script("bench.smalls", k_smalls_source, &ctx);
        script.parse();
        script.resolve();

        nw::smalls::BytecodeModule module("bench");
        nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), &ctx);
        compiler.compile();
        benchmark::DoNotOptimize(module.functions.size());
    }
}
BENCHMARK(BM_smalls_compile);

static void BM_smalls_execute(benchmark::State& state)
{
    nw::smalls::BytecodeModule module("bench");

    {
        nw::MemoryArena arena(nw::MB(128));
        nw::MemoryScope scope(&arena);
        nw::smalls::Context ctx;
        ctx.arena = &arena;
        ctx.scope = &scope;

        nw::smalls::Script script("bench.smalls", k_smalls_source, &ctx);
        script.parse();
        script.resolve();

        nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), &ctx);
        compiler.compile();
    }

    nw::smalls::VirtualMachine vm{};
    auto* gc = nw::kernel::runtime().gc();
    size_t iters_since_gc = 0;
    constexpr size_t k_gc_interval = 256;

    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);

        if (gc && ++iters_since_gc >= k_gc_interval) {
            state.PauseTiming();
            gc->collect_minor();
            state.ResumeTiming();
            iters_since_gc = 0;
        }
    }

    if (gc) {
        gc->collect_major();
    }
}
BENCHMARK(BM_smalls_execute);

static void BM_smalls_gc_minor(benchmark::State& state)
{
    auto& rt = nw::kernel::runtime();
    auto* gc = rt.gc();
    if (!gc) {
        state.SkipWithError("runtime.gc() returned null");
        return;
    }

    // Clear engine-owned/borrowed handles accumulated by game data loading so
    // enumerate_handle_roots doesn't iterate thousands of stale entries on every
    // mark_roots call, which would dominate the measurement.
    rt.clear_non_vm_owned_handle_registry();
    rt.prune_stale_handle_registry();

    for (auto _ : state) {
        for (int i = 0; i < 10000; ++i) {
            char buf[32];
            const int n = std::snprintf(buf, sizeof(buf), "s%08d", i);
            rt.alloc_string(std::string_view(buf, n > 0 ? static_cast<size_t>(n) : 0));
        }
        gc->collect_minor();
    }
    state.SetItemsProcessed(10000 * state.iterations());
}
BENCHMARK(BM_smalls_gc_minor);

static void BM_smalls_gc_major(benchmark::State& state)
{
    auto& rt = nw::kernel::runtime();
    auto* gc = rt.gc();
    if (!gc) {
        state.SkipWithError("runtime.gc() returned null");
        return;
    }

    // Same as gc_minor: prune stale handles so sweep_all and mark_roots only
    // see objects actually allocated by this benchmark.
    rt.clear_non_vm_owned_handle_registry();
    rt.prune_stale_handle_registry();

    for (auto _ : state) {
        for (int i = 0; i < 20000; ++i) {
            char buf[32];
            const int n = std::snprintf(buf, sizeof(buf), "m%08d", i);
            rt.alloc_string(std::string_view(buf, n > 0 ? static_cast<size_t>(n) : 0));
        }
        gc->collect_major();
    }
    state.SetItemsProcessed(20000 * state.iterations());
}
BENCHMARK(BM_smalls_gc_major);

// == VM micro-benchmarks =====================================================
// One benchmark per hotspot identified in the performance review.
// Each module is compiled once; only VM execution is measured.

static nw::smalls::BytecodeModule vm_compile(const char* source)
{
    nw::smalls::BytecodeModule module("bench");
    nw::MemoryArena arena(nw::MB(64));
    nw::MemoryScope scope(&arena);
    nw::smalls::Context ctx;
    ctx.arena = &arena;
    ctx.scope = &scope;
    nw::smalls::Script script("bench.smalls", source, &ctx);
    script.parse();
    script.resolve();
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), &ctx);
    compiler.compile();
    return module;
}

// Hotspot: reg() re-fetching frames_.back(), nw::kernel::runtime() per opcode.
// Pure integer arithmetic loop — ADD/LOADI/ISEQ/JMPF only, no heap traffic.
static void BM_vm_int_arith_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
fn main(): int {
    var sum = 0;
    var i = 0;
    for (i < 1000) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
)");
    nw::smalls::VirtualMachine vm{};
    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_vm_int_arith_loop);

// Hotspot: setup_script_call allocates Vector<Value> on every call.
// Calls a 2-argument function 1000 times per VM execution.
static void BM_vm_call_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
fn add(a: int, b: int): int {
    return a + b;
}
fn main(): int {
    var sum = 0;
    var i = 0;
    for (i < 1000) {
        sum = add(sum, i);
        i = i + 1;
    }
    return sum;
}
)");
    nw::smalls::VirtualMachine vm{};
    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_vm_call_loop);

// Hotspot: evaluate_truthiness() called as a function on every JMPT/JMPF.
// Loop controlled by a bool variable — no int comparison, pure bool branch.
static void BM_vm_bool_branch(benchmark::State& state)
{
    auto module = vm_compile(R"(
fn main(): int {
    var count = 0;
    var running: bool = true;
    for (running) {
        count = count + 1;
        running = count < 1000;
    }
    return count;
}
)");
    nw::smalls::VirtualMachine vm{};
    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_vm_bool_branch);

// Hotspot: Opcode::NOT has no fast path — always goes to runtime dispatch.
// 1000 bool NOT operations per execution.
static void BM_vm_not_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
fn main(): bool {
    var flag = true;
    var i = 0;
    for (i < 1000) {
        flag = !flag;
        i = i + 1;
    }
    return flag;
}
)");
    nw::smalls::VirtualMachine vm{};
    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_vm_not_loop);

// Hotspot: op_field_access() double-dispatch — 24 opcodes funnel to one
// function that switches on op again, then calls read/write_field_at_offset.
// 1000 iterations of read-modify-write on two int struct fields.
static void BM_vm_field_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
type Point {
    x: int;
    y: int;
};
fn main(): int {
    var p: Point = { 0, 0 };
    var i = 0;
    for (i < 1000) {
        p.x = p.x + 1;
        p.y = p.y + 2;
        i = i + 1;
    }
    return p.x + p.y;
}
)");
    nw::smalls::VirtualMachine vm{};
    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_vm_field_loop);

// Closure allocation + upvalue capture + closure dispatch.
// make_adder captures one int upvalue; the returned closure is called 1000x.
static void BM_vm_closure_call_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
fn make_adder(n: int): fn(int): int {
    return fn(x: int): int { return x + n; };
}
fn main(): int {
    var add = make_adder(1);
    var sum = 0;
    var i = 0;
    for (i < 1000) {
        sum = add(sum);
        i = i + 1;
    }
    return sum;
}
)");
    nw::smalls::VirtualMachine vm{};
    auto* gc = nw::kernel::runtime().gc();
    size_t iters_since_gc = 0;
    constexpr size_t k_gc_interval = 256;

    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);

        if (gc && ++iters_since_gc >= k_gc_interval) {
            state.PauseTiming();
            gc->collect_minor();
            state.ResumeTiming();
            iters_since_gc = 0;
        }
    }
    state.SetItemsProcessed(1000 * state.iterations());

    if (gc) {
        gc->collect_major();
    }
}
BENCHMARK(BM_vm_closure_call_loop);

// String heap allocation rate and GC pressure.
// 100 str.append calls per execution build up a growing string.
static void BM_vm_string_concat_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
import core.string as str;
fn main(): string {
    var s = "";
    var i = 0;
    for (i < 100) {
        s = str.append(s, "hello");
        i = i + 1;
    }
    return s;
}
)");
    nw::smalls::VirtualMachine vm{};
    auto* gc = nw::kernel::runtime().gc();
    size_t iters_since_gc = 0;
    constexpr size_t k_gc_interval = 64;

    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);

        if (gc && ++iters_since_gc >= k_gc_interval) {
            state.PauseTiming();
            gc->collect_minor();
            state.ResumeTiming();
            iters_since_gc = 0;
        }
    }
    state.SetItemsProcessed(100 * state.iterations());

    if (gc) {
        gc->collect_major();
    }
}
BENCHMARK(BM_vm_string_concat_loop);

// Map boxing overhead: Value hashing/equality, absl hash map through smalls.
// 100 puts + 100 gets per execution.
static void BM_vm_map_put_get_loop(benchmark::State& state)
{
    auto module = vm_compile(R"(
fn main(): int {
    var m: map!(int, int) = {};
    var sum = 0;
    var i = 0;
    for (i < 100) {
        m[i] = i * 2;
        sum = sum + m[i];
        i = i + 1;
    }
    return sum;
}
)");
    nw::smalls::VirtualMachine vm{};
    auto* gc = nw::kernel::runtime().gc();
    size_t iters_since_gc = 0;
    constexpr size_t k_gc_interval = 128;

    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);

        if (gc && ++iters_since_gc >= k_gc_interval) {
            state.PauseTiming();
            gc->collect_minor();
            state.ResumeTiming();
            iters_since_gc = 0;
        }
    }
    state.SetItemsProcessed(200 * state.iterations());

    if (gc) {
        gc->collect_major();
    }
}
BENCHMARK(BM_vm_map_put_get_loop);

// Minimum C++→smalls boundary cost: module lookup, function name lookup, frame setup.
// Uses execute_script (not execute_compiled) to exercise the full dispatch path.
static void BM_smalls_script_entry(benchmark::State& state)
{
    nw::MemoryArena arena(nw::MB(4));
    nw::MemoryScope scope(&arena);
    nw::smalls::Context ctx;
    ctx.arena = &arena;
    ctx.scope = &scope;
    nw::smalls::Script script("bench_entry.smalls", "fn noop(): int { return 0; }", &ctx);
    script.parse();
    script.resolve();

    auto& rt = nw::kernel::runtime();
    // Warm up: force module compilation and populate the cache.
    rt.execute_script(&script, "noop");

    for (auto _ : state) {
        auto res = rt.execute_script(&script, "noop");
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_smalls_script_entry);
