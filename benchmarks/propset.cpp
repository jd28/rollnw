#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>
#include <nw/smalls/GarbageCollector.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

#include <filesystem>

namespace nwk = nw::kernel;
namespace fs = std::filesystem;

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

        [[propset]]
        type BenchDyn {
            nums: array!(int);
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

        import core.array as arr;

        fn dyn_seed(target: Creature, count: int): int {
            var d = get_propset!(BenchDyn)(target);
            var i = 0;
            for (i < count) {
                arr.push(d.nums, i);
                i = i + 1;
            }
            return arr.len(d.nums);
        }

        fn dyn_mutate_sparse(target: Creature, value: int): int {
            var d = get_propset!(BenchDyn)(target);
            arr.push(d.nums, value);
            var n = arr.len(d.nums);
            if (n > 64) {
                arr.set(d.nums, 0, arr.get(d.nums, 0) + 1);
                arr.clear(d.nums);
            }
            return n;
        }

        fn dyn_mutate_dense(target: Creature, iters: int): int {
            var d = get_propset!(BenchDyn)(target);
            var i = 0;
            for (i < iters) {
                arr.push(d.nums, i);
                if (arr.len(d.nums) > 64) {
                    arr.clear(d.nums);
                }
                i = i + 1;
            }
            return arr.len(d.nums);
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

nw::Creature* load_bridge_bench_creature()
{
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    if (creature) {
        return creature;
    }

    const fs::path repo_root = fs::path(__FILE__).parent_path().parent_path();
    const fs::path test_data_fallback = repo_root / "tests/test_data/user/development/drorry.utc";
    creature = nwk::objects().load_file<nw::Creature>(test_data_fallback.string());
    if (creature) {
        return creature;
    }

    const fs::path legacy_fallback = repo_root / "test_data/user/development/drorry.utc";
    return nwk::objects().load_file<nw::Creature>(legacy_fallback.string());
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

static void BM_bridge_field_access(benchmark::State& state)
{
    auto* creature = load_bridge_bench_creature();
    if (!creature) {
        state.SkipWithError("failed to load drorry.utc");
        return;
    }

    for (auto _ : state) {
        int out = 0;
        for (int i = 0; i < 1000; ++i) {
            out += nwn1::get_ability_score(creature, nwn1::ability_strength, false);
            out += nwn1::get_ability_score(creature, nwn1::ability_dexterity, false);
            out += nwn1::get_ability_score(creature, nwn1::ability_constitution, false);
        }
        benchmark::DoNotOptimize(out);
    }

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_bridge_field_access);

static void BM_propset_gc_minor_sparse_dirty_scan(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* gc = rt.gc();
    if (!gc) {
        state.SkipWithError("gc unavailable");
        return;
    }

    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    const int object_count = static_cast<int>(state.range(0));
    nw::Vector<nw::Creature*> creatures;
    creatures.reserve(object_count);

    nw::Vector<nw::smalls::Value> seed_args;
    seed_args.push_back(nw::smalls::Value{});
    seed_args.push_back(nw::smalls::Value::make_int(16));

    for (int i = 0; i < object_count; ++i) {
        auto* creature = nwk::objects().make<nw::Creature>();
        if (!creature) {
            state.SkipWithError("failed to allocate creature");
            return;
        }
        creatures.push_back(creature);
        seed_args[0] = as_creature_value(creature);
        auto seed = rt.execute_script(script, "dyn_seed", seed_args);
        if (!seed.ok()) {
            state.SkipWithError("failed to seed dynamic propset");
            return;
        }
    }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value{});
    args.push_back(nw::smalls::Value::make_int(0));
    int idx = 0;

    for (auto _ : state) {
        auto* creature = creatures[idx % object_count];
        args[0] = as_creature_value(creature);
        args[1] = nw::smalls::Value::make_int(idx);
        auto res = rt.execute_script(script, "dyn_mutate_sparse", args);
        benchmark::DoNotOptimize(res);
        gc->collect_minor();
        ++idx;
    }

    for (auto* creature : creatures) {
        nwk::objects().destroy(creature->handle());
    }
}
BENCHMARK(BM_propset_gc_minor_sparse_dirty_scan)->Arg(128)->Arg(512)->Arg(2048);

static void BM_propset_gc_minor_dense_mutation(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* gc = rt.gc();
    if (!gc) {
        state.SkipWithError("gc unavailable");
        return;
    }

    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    const int object_count = static_cast<int>(state.range(0));
    nw::Vector<nw::Creature*> creatures;
    creatures.reserve(object_count);

    for (int i = 0; i < object_count; ++i) {
        auto* creature = nwk::objects().make<nw::Creature>();
        if (!creature) {
            state.SkipWithError("failed to allocate creature");
            return;
        }
        creatures.push_back(creature);
    }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value{});
    args.push_back(nw::smalls::Value::make_int(8));

    for (auto _ : state) {
        for (auto* creature : creatures) {
            args[0] = as_creature_value(creature);
            auto res = rt.execute_script(script, "dyn_mutate_dense", args);
            benchmark::DoNotOptimize(res);
        }
        gc->collect_minor();
    }

    for (auto* creature : creatures) {
        nwk::objects().destroy(creature->handle());
    }
}
BENCHMARK(BM_propset_gc_minor_dense_mutation)->Arg(128)->Arg(512);

static void BM_propset_gc_major_incremental_alloc(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* gc = rt.gc();
    if (!gc) {
        state.SkipWithError("gc unavailable");
        return;
    }

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
    args.push_back(as_creature_value(creature));
    args.push_back(nw::smalls::Value::make_int(0));

    const int allocs_per_iter = static_cast<int>(state.range(0));

    for (auto _ : state) {
        gc->start_major_gc();

        for (int i = 0; i < allocs_per_iter; ++i) {
            nw::smalls::HeapPtr s = rt.alloc_string("major mark alloc");
            benchmark::DoNotOptimize(s);

            args[1] = nw::smalls::Value::make_int(i);
            auto res = rt.execute_script(script, "dyn_mutate_sparse", args);
            benchmark::DoNotOptimize(res);
        }

        while (gc->phase() == nw::smalls::GCPhase::mark_incremental && !gc->mark_step(256)) {
        }
        if (gc->phase() == nw::smalls::GCPhase::mark_incremental) {
            gc->finish_major_gc();
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(allocs_per_iter));

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_propset_gc_major_incremental_alloc)->Arg(32)->Arg(128);

static void BM_propset_get_ref_roundtrip(benchmark::State& state)
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
    args.push_back(as_creature_value(creature));
    args.push_back(nw::smalls::Value::make_int(1));

    for (auto _ : state) {
        auto res = rt.execute_script(script, "write_loop", args);
        benchmark::DoNotOptimize(res);
    }

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_propset_get_ref_roundtrip);
