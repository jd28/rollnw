#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Memory.hpp>
#include <nw/smalls/AstCompiler.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/VirtualMachine.hpp>

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
    for (auto _ : state) {
        auto res = vm.execute(&module, "main", {});
        benchmark::DoNotOptimize(res);
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

    for (auto _ : state) {
        for (int i = 0; i < 10000; ++i) {
            char buf[32];
            const int n = std::snprintf(buf, sizeof(buf), "s%08d", i);
            rt.alloc_string(std::string_view(buf, n > 0 ? static_cast<size_t>(n) : 0));
        }
        gc->collect_minor();
    }
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

    for (auto _ : state) {
        for (int i = 0; i < 20000; ++i) {
            char buf[32];
            const int n = std::snprintf(buf, sizeof(buf), "m%08d", i);
            rt.alloc_string(std::string_view(buf, n > 0 ? static_cast<size_t>(n) : 0));
        }
        gc->collect_major();
    }
}
BENCHMARK(BM_smalls_gc_major);
