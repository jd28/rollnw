#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

namespace nwk = nw::kernel;

namespace {

nw::smalls::Script* ensure_propset_bench_module()
{
    static nw::smalls::Script* script = nullptr;
    if (script) {
        return script;
    }

    constexpr const char* k_source = R"(
        [[intrinsic("get_propset")]] fn get_propset(obj: object): $T;

        [[propset]]
        type BenchStats {
            str: int;
            dex: int;
            con: int;
        };

        fn touch(target: Creature): int {
            var s = get_propset!(BenchStats)(target);
            if (s.str == 0) {
                s.str = 10;
                s.dex = 12;
                s.con = 14;
            }
            return s.str;
        }

        fn read_loop(target: Creature, iters: int): int {
            var s = get_propset!(BenchStats)(target);
            var sum = 0;
            var i = 0;
            for (i < iters) {
                sum = sum + s.str + s.dex + s.con;
                i = i + 1;
            }
            return sum;
        }

        fn write_loop(target: Creature, iters: int): int {
            var s = get_propset!(BenchStats)(target);
            var i = 0;
            for (i < iters) {
                s.str = s.str + 1;
                s.dex = s.dex + 1;
                i = i + 1;
            }
            return s.str;
        }
    )";

    script = nw::kernel::runtime().load_module_from_source("bench.propset", k_source);
    return script;
}

nw::smalls::Value as_creature_value(nw::Creature* creature)
{
    auto& rt = nw::kernel::runtime();
    auto v = nw::smalls::Value::make_object(creature->handle());
    v.type_id = rt.object_subtype_for_tag(creature->handle().type);
    return v;
}

} // namespace

static void BM_propset_alloc_free(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    for (auto _ : state) {
        auto* creature = nwk::objects().make<nw::Creature>();
        if (!creature) {
            state.SkipWithError("failed to allocate creature");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        args.push_back(as_creature_value(creature));
        auto res = rt.execute_script(script, "touch", args);
        benchmark::DoNotOptimize(res);
        nwk::objects().destroy(creature->handle());
    }
}
BENCHMARK(BM_propset_alloc_free);

static void BM_propset_script_field_access(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to allocate creature");
        return;
    }

    nw::Vector<nw::smalls::Value> args;
    constexpr int k_loop_iters = 1000;
    args.push_back(as_creature_value(creature));
    args.push_back(nw::smalls::Value::make_int(k_loop_iters));

    for (auto _ : state) {
        auto res = rt.execute_script(script, "read_loop", args);
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(k_loop_iters) * 3);

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_propset_script_field_access);

static void BM_propset_field_write(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to allocate creature");
        return;
    }

    nw::Vector<nw::smalls::Value> args;
    constexpr int k_loop_iters = 1000;
    args.push_back(as_creature_value(creature));
    args.push_back(nw::smalls::Value::make_int(k_loop_iters));

    for (auto _ : state) {
        auto res = rt.execute_script(script, "write_loop", args);
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(k_loop_iters) * 2);

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_propset_field_write);
