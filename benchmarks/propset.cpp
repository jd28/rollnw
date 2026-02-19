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

struct HandleRegistryDiag {
    uint64_t total = 0;
    uint64_t vm_owned = 0;
    uint64_t engine_owned = 0;
    uint64_t borrowed = 0;
    uint64_t non_vm_owned = 0;
    uint64_t stale_null_cell = 0;
    uint64_t stale_no_header = 0;
    uint64_t stale_non_handle_cell = 0;
    uint64_t stale_mismatched_handle = 0;
    uint64_t valid_non_vm_owned = 0;
};

HandleRegistryDiag collect_handle_registry_diag(nw::smalls::Runtime& rt)
{
    HandleRegistryDiag out;
    out.total = rt.global_handle_registry_.size();

    for (const auto& [h, entry] : rt.global_handle_registry_) {
        switch (entry.mode) {
        case nw::smalls::OwnershipMode::VM_OWNED:
            ++out.vm_owned;
            break;
        case nw::smalls::OwnershipMode::ENGINE_OWNED:
            ++out.engine_owned;
            ++out.non_vm_owned;
            break;
        case nw::smalls::OwnershipMode::BORROWED:
            ++out.borrowed;
            ++out.non_vm_owned;
            break;
        }

        if (entry.mode == nw::smalls::OwnershipMode::VM_OWNED) {
            continue;
        }

        if (entry.vm_value.value == 0) {
            ++out.stale_null_cell;
            continue;
        }

        auto* header = rt.heap_.try_get_header(entry.vm_value);
        if (!header) {
            ++out.stale_no_header;
            continue;
        }
        if (!rt.is_handle_type(header->type_id)) {
            ++out.stale_non_handle_cell;
            continue;
        }

        const nw::TypedHandle* cell_handle = rt.get_handle(entry.vm_value);
        if (!cell_handle || *cell_handle != h) {
            ++out.stale_mismatched_handle;
            continue;
        }

        ++out.valid_non_vm_owned;
    }

    return out;
}

struct GameTickGCPolicy {
    size_t minor_step_budget = 4096;
    uint32_t full_minor_every_ticks = 24;
    size_t major_step_budget = 256;
    uint32_t major_start_every_ticks = 600;
    double young_pressure_threshold = 0.20;
};

void run_gc_game_tick(nw::smalls::Runtime& rt, nw::smalls::GarbageCollector* gc, uint64_t tick, const GameTickGCPolicy& policy)
{
    if (!gc) {
        return;
    }

    const size_t committed = rt.heap_.committed();
    const size_t young_bytes = rt.heap_.young_bytes();
    const bool high_young_pressure = committed > 0
        && static_cast<double>(young_bytes) > static_cast<double>(committed) * policy.young_pressure_threshold;
    const bool periodic_minor = (tick % policy.full_minor_every_ticks) == 0;
    if (gc->is_minor_collecting() || high_young_pressure || periodic_minor) {
        gc->collect_minor_step(policy.minor_step_budget);
    }

    if (tick > 0 && (tick % policy.major_start_every_ticks) == 0 && gc->phase() == nw::smalls::GCPhase::idle) {
        gc->start_major_gc();
    }

    if (gc->phase() == nw::smalls::GCPhase::mark_incremental) {
        if (gc->mark_step(policy.major_step_budget)) {
            gc->finish_major_gc();
        }
    }
}

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

    const size_t cleared_non_vm_before = rt.clear_non_vm_owned_handle_registry();
    const size_t pruned_before = rt.prune_stale_handle_registry();
    state.counters["gc_handle_registry_cleared_non_vm_pre"] = static_cast<double>(cleared_non_vm_before);
    state.counters["gc_handle_registry_pruned_pre"] = static_cast<double>(pruned_before);

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

    const size_t cleared_non_vm_before = rt.clear_non_vm_owned_handle_registry();
    const size_t pruned_before = rt.prune_stale_handle_registry();
    state.counters["gc_handle_registry_cleared_non_vm_pre"] = static_cast<double>(cleared_non_vm_before);
    state.counters["gc_handle_registry_pruned_pre"] = static_cast<double>(pruned_before);

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

    const size_t cleared_non_vm_before = rt.clear_non_vm_owned_handle_registry();
    const size_t pruned_before = rt.prune_stale_handle_registry();
    state.counters["gc_handle_registry_cleared_non_vm_pre"] = static_cast<double>(cleared_non_vm_before);
    state.counters["gc_handle_registry_pruned_pre"] = static_cast<double>(pruned_before);

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

    const size_t cleared_non_vm_before = rt.clear_non_vm_owned_handle_registry();
    const size_t pruned_before = rt.prune_stale_handle_registry();
    state.counters["gc_handle_registry_cleared_non_vm_pre"] = static_cast<double>(cleared_non_vm_before);
    state.counters["gc_handle_registry_pruned_pre"] = static_cast<double>(pruned_before);

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

    const auto stats_before = gc->stats();

    for (auto _ : state) {
        auto* creature = creatures[idx % object_count];
        args[0] = as_creature_value(creature);
        args[1] = nw::smalls::Value::make_int(idx);
        auto res = rt.execute_script(script, "dyn_mutate_sparse", args);
        benchmark::DoNotOptimize(res);
        gc->collect_minor();
        ++idx;
    }

    const auto stats_after = gc->stats();
    const uint64_t minor_collections = stats_after.minor_collections - stats_before.minor_collections;
    if (minor_collections > 0) {
        const HandleRegistryDiag handle_diag = collect_handle_registry_diag(rt);
        const double minor_count = static_cast<double>(minor_collections);
        const uint64_t roots_us = stats_after.minor_roots_time_us - stats_before.minor_roots_time_us;
        const uint64_t roots_frame_slots_us = stats_after.minor_roots_frame_slots_us - stats_before.minor_roots_frame_slots_us;
        const uint64_t roots_registers_us = stats_after.minor_roots_registers_us - stats_before.minor_roots_registers_us;
        const uint64_t roots_runtime_stack_us = stats_after.minor_roots_runtime_stack_us - stats_before.minor_roots_runtime_stack_us;
        const uint64_t roots_module_globals_us = stats_after.minor_roots_module_globals_us - stats_before.minor_roots_module_globals_us;
        const uint64_t roots_handle_roots_us = stats_after.minor_roots_handle_roots_us - stats_before.minor_roots_handle_roots_us;
        const uint64_t remembered_us = stats_after.minor_remembered_scan_time_us - stats_before.minor_remembered_scan_time_us;
        const uint64_t trace_us = stats_after.minor_trace_time_us - stats_before.minor_trace_time_us;
        const uint64_t sweep_us = stats_after.minor_sweep_promote_time_us - stats_before.minor_sweep_promote_time_us;
        const uint64_t phase_total_us = roots_us + remembered_us + trace_us + sweep_us;

        const uint64_t stack_slots_total = stats_after.minor_stack_slots_total - stats_before.minor_stack_slots_total;
        const uint64_t stack_slots_scanned = stats_after.minor_stack_slots_scanned - stats_before.minor_stack_slots_scanned;
        const uint64_t stack_slots_skipped = stats_after.minor_stack_slots_skipped - stats_before.minor_stack_slots_skipped;
        const uint64_t register_values_total = stats_after.minor_register_values_total - stats_before.minor_register_values_total;
        const uint64_t register_values_heap = stats_after.minor_register_values_heap - stats_before.minor_register_values_heap;
        const uint64_t runtime_stack_values_total = stats_after.minor_runtime_stack_values_total - stats_before.minor_runtime_stack_values_total;
        const uint64_t runtime_stack_values_heap = stats_after.minor_runtime_stack_values_heap - stats_before.minor_runtime_stack_values_heap;
        const uint64_t module_global_roots_visited = stats_after.minor_module_global_roots_visited - stats_before.minor_module_global_roots_visited;
        const uint64_t handle_roots_visited = stats_after.minor_handle_roots_visited - stats_before.minor_handle_roots_visited;

        state.counters["gc_minor_roots_us"] = static_cast<double>(roots_us) / minor_count;
        state.counters["gc_minor_roots_frame_slots_us"] = static_cast<double>(roots_frame_slots_us) / minor_count;
        state.counters["gc_minor_roots_registers_us"] = static_cast<double>(roots_registers_us) / minor_count;
        state.counters["gc_minor_roots_runtime_stack_us"] = static_cast<double>(roots_runtime_stack_us) / minor_count;
        state.counters["gc_minor_roots_module_globals_us"] = static_cast<double>(roots_module_globals_us) / minor_count;
        state.counters["gc_minor_roots_handle_roots_us"] = static_cast<double>(roots_handle_roots_us) / minor_count;
        state.counters["gc_minor_remembered_us"] = static_cast<double>(remembered_us) / minor_count;
        state.counters["gc_minor_trace_us"] = static_cast<double>(trace_us) / minor_count;
        state.counters["gc_minor_sweep_us"] = static_cast<double>(sweep_us) / minor_count;
        state.counters["gc_minor_phase_total_us"] = static_cast<double>(phase_total_us) / minor_count;
        state.counters["gc_minor_stack_slots_total"] = static_cast<double>(stack_slots_total) / minor_count;
        state.counters["gc_minor_stack_slots_scanned"] = static_cast<double>(stack_slots_scanned) / minor_count;
        state.counters["gc_minor_stack_slots_skipped"] = static_cast<double>(stack_slots_skipped) / minor_count;
        state.counters["gc_minor_register_values_total"] = static_cast<double>(register_values_total) / minor_count;
        state.counters["gc_minor_register_values_heap"] = static_cast<double>(register_values_heap) / minor_count;
        state.counters["gc_minor_runtime_stack_values_total"] = static_cast<double>(runtime_stack_values_total) / minor_count;
        state.counters["gc_minor_runtime_stack_values_heap"] = static_cast<double>(runtime_stack_values_heap) / minor_count;
        state.counters["gc_minor_module_global_roots_visited"] = static_cast<double>(module_global_roots_visited) / minor_count;
        state.counters["gc_minor_handle_roots_visited"] = static_cast<double>(handle_roots_visited) / minor_count;
        state.counters["gc_handle_registry_total"] = static_cast<double>(handle_diag.total);
        state.counters["gc_handle_registry_vm_owned"] = static_cast<double>(handle_diag.vm_owned);
        state.counters["gc_handle_registry_engine_owned"] = static_cast<double>(handle_diag.engine_owned);
        state.counters["gc_handle_registry_borrowed"] = static_cast<double>(handle_diag.borrowed);
        state.counters["gc_handle_registry_non_vm_owned"] = static_cast<double>(handle_diag.non_vm_owned);
        state.counters["gc_handle_registry_valid_non_vm_owned"] = static_cast<double>(handle_diag.valid_non_vm_owned);
        state.counters["gc_handle_registry_stale_null_cell"] = static_cast<double>(handle_diag.stale_null_cell);
        state.counters["gc_handle_registry_stale_no_header"] = static_cast<double>(handle_diag.stale_no_header);
        state.counters["gc_handle_registry_stale_non_handle_cell"] = static_cast<double>(handle_diag.stale_non_handle_cell);
        state.counters["gc_handle_registry_stale_mismatch"] = static_cast<double>(handle_diag.stale_mismatched_handle);

        if (phase_total_us > 0) {
            const double denom = static_cast<double>(phase_total_us);
            state.counters["gc_minor_roots_pct"] = 100.0 * static_cast<double>(roots_us) / denom;
            state.counters["gc_minor_trace_pct"] = 100.0 * static_cast<double>(trace_us) / denom;
        }
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

    const auto stats_before = gc->stats();

    for (auto _ : state) {
        for (auto* creature : creatures) {
            args[0] = as_creature_value(creature);
            auto res = rt.execute_script(script, "dyn_mutate_dense", args);
            benchmark::DoNotOptimize(res);
        }
        gc->collect_minor();
    }

    const auto stats_after = gc->stats();
    const uint64_t minor_collections = stats_after.minor_collections - stats_before.minor_collections;
    if (minor_collections > 0) {
        const HandleRegistryDiag handle_diag = collect_handle_registry_diag(rt);
        const double minor_count = static_cast<double>(minor_collections);
        const uint64_t roots_us = stats_after.minor_roots_time_us - stats_before.minor_roots_time_us;
        const uint64_t roots_frame_slots_us = stats_after.minor_roots_frame_slots_us - stats_before.minor_roots_frame_slots_us;
        const uint64_t roots_registers_us = stats_after.minor_roots_registers_us - stats_before.minor_roots_registers_us;
        const uint64_t roots_runtime_stack_us = stats_after.minor_roots_runtime_stack_us - stats_before.minor_roots_runtime_stack_us;
        const uint64_t roots_module_globals_us = stats_after.minor_roots_module_globals_us - stats_before.minor_roots_module_globals_us;
        const uint64_t roots_handle_roots_us = stats_after.minor_roots_handle_roots_us - stats_before.minor_roots_handle_roots_us;
        const uint64_t remembered_us = stats_after.minor_remembered_scan_time_us - stats_before.minor_remembered_scan_time_us;
        const uint64_t trace_us = stats_after.minor_trace_time_us - stats_before.minor_trace_time_us;
        const uint64_t sweep_us = stats_after.minor_sweep_promote_time_us - stats_before.minor_sweep_promote_time_us;
        const uint64_t phase_total_us = roots_us + remembered_us + trace_us + sweep_us;

        const uint64_t stack_slots_total = stats_after.minor_stack_slots_total - stats_before.minor_stack_slots_total;
        const uint64_t stack_slots_scanned = stats_after.minor_stack_slots_scanned - stats_before.minor_stack_slots_scanned;
        const uint64_t stack_slots_skipped = stats_after.minor_stack_slots_skipped - stats_before.minor_stack_slots_skipped;
        const uint64_t register_values_total = stats_after.minor_register_values_total - stats_before.minor_register_values_total;
        const uint64_t register_values_heap = stats_after.minor_register_values_heap - stats_before.minor_register_values_heap;
        const uint64_t runtime_stack_values_total = stats_after.minor_runtime_stack_values_total - stats_before.minor_runtime_stack_values_total;
        const uint64_t runtime_stack_values_heap = stats_after.minor_runtime_stack_values_heap - stats_before.minor_runtime_stack_values_heap;
        const uint64_t module_global_roots_visited = stats_after.minor_module_global_roots_visited - stats_before.minor_module_global_roots_visited;
        const uint64_t handle_roots_visited = stats_after.minor_handle_roots_visited - stats_before.minor_handle_roots_visited;

        state.counters["gc_minor_roots_us"] = static_cast<double>(roots_us) / minor_count;
        state.counters["gc_minor_roots_frame_slots_us"] = static_cast<double>(roots_frame_slots_us) / minor_count;
        state.counters["gc_minor_roots_registers_us"] = static_cast<double>(roots_registers_us) / minor_count;
        state.counters["gc_minor_roots_runtime_stack_us"] = static_cast<double>(roots_runtime_stack_us) / minor_count;
        state.counters["gc_minor_roots_module_globals_us"] = static_cast<double>(roots_module_globals_us) / minor_count;
        state.counters["gc_minor_roots_handle_roots_us"] = static_cast<double>(roots_handle_roots_us) / minor_count;
        state.counters["gc_minor_remembered_us"] = static_cast<double>(remembered_us) / minor_count;
        state.counters["gc_minor_trace_us"] = static_cast<double>(trace_us) / minor_count;
        state.counters["gc_minor_sweep_us"] = static_cast<double>(sweep_us) / minor_count;
        state.counters["gc_minor_phase_total_us"] = static_cast<double>(phase_total_us) / minor_count;
        state.counters["gc_minor_stack_slots_total"] = static_cast<double>(stack_slots_total) / minor_count;
        state.counters["gc_minor_stack_slots_scanned"] = static_cast<double>(stack_slots_scanned) / minor_count;
        state.counters["gc_minor_stack_slots_skipped"] = static_cast<double>(stack_slots_skipped) / minor_count;
        state.counters["gc_minor_register_values_total"] = static_cast<double>(register_values_total) / minor_count;
        state.counters["gc_minor_register_values_heap"] = static_cast<double>(register_values_heap) / minor_count;
        state.counters["gc_minor_runtime_stack_values_total"] = static_cast<double>(runtime_stack_values_total) / minor_count;
        state.counters["gc_minor_runtime_stack_values_heap"] = static_cast<double>(runtime_stack_values_heap) / minor_count;
        state.counters["gc_minor_module_global_roots_visited"] = static_cast<double>(module_global_roots_visited) / minor_count;
        state.counters["gc_minor_handle_roots_visited"] = static_cast<double>(handle_roots_visited) / minor_count;
        state.counters["gc_handle_registry_total"] = static_cast<double>(handle_diag.total);
        state.counters["gc_handle_registry_vm_owned"] = static_cast<double>(handle_diag.vm_owned);
        state.counters["gc_handle_registry_engine_owned"] = static_cast<double>(handle_diag.engine_owned);
        state.counters["gc_handle_registry_borrowed"] = static_cast<double>(handle_diag.borrowed);
        state.counters["gc_handle_registry_non_vm_owned"] = static_cast<double>(handle_diag.non_vm_owned);
        state.counters["gc_handle_registry_valid_non_vm_owned"] = static_cast<double>(handle_diag.valid_non_vm_owned);
        state.counters["gc_handle_registry_stale_null_cell"] = static_cast<double>(handle_diag.stale_null_cell);
        state.counters["gc_handle_registry_stale_no_header"] = static_cast<double>(handle_diag.stale_no_header);
        state.counters["gc_handle_registry_stale_non_handle_cell"] = static_cast<double>(handle_diag.stale_non_handle_cell);
        state.counters["gc_handle_registry_stale_mismatch"] = static_cast<double>(handle_diag.stale_mismatched_handle);

        if (phase_total_us > 0) {
            const double denom = static_cast<double>(phase_total_us);
            state.counters["gc_minor_roots_pct"] = 100.0 * static_cast<double>(roots_us) / denom;
            state.counters["gc_minor_trace_pct"] = 100.0 * static_cast<double>(trace_us) / denom;
        }
    }

    for (auto* creature : creatures) {
        nwk::objects().destroy(creature->handle());
    }
}
BENCHMARK(BM_propset_gc_minor_dense_mutation)->Arg(128)->Arg(512);

static void BM_propset_gc_tick_sparse_dirty_scan(benchmark::State& state)
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

    GameTickGCPolicy policy;
    auto saved_cfg = gc->config();
    auto tick_cfg = saved_cfg;
    tick_cfg.minor_step_time_budget_us = 2000;
    gc->set_config(tick_cfg);
    uint64_t tick = 1;
    int idx = 0;

    for (auto _ : state) {
        auto* creature = creatures[idx % object_count];
        args[0] = as_creature_value(creature);
        args[1] = nw::smalls::Value::make_int(idx);
        auto res = rt.execute_script(script, "dyn_mutate_sparse", args);
        benchmark::DoNotOptimize(res);
        run_gc_game_tick(rt, gc, tick++, policy);
        ++idx;
    }

    for (auto* creature : creatures) {
        nwk::objects().destroy(creature->handle());
    }
    gc->set_config(saved_cfg);
}
BENCHMARK(BM_propset_gc_tick_sparse_dirty_scan)->Arg(128)->Arg(512)->Arg(2048);

static void BM_propset_gc_tick_dense_mutation(benchmark::State& state)
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

    GameTickGCPolicy policy;
    auto saved_cfg = gc->config();
    auto tick_cfg = saved_cfg;
    tick_cfg.minor_step_time_budget_us = 2000;
    gc->set_config(tick_cfg);
    uint64_t tick = 1;

    for (auto _ : state) {
        for (auto* creature : creatures) {
            args[0] = as_creature_value(creature);
            auto res = rt.execute_script(script, "dyn_mutate_dense", args);
            benchmark::DoNotOptimize(res);
        }
        run_gc_game_tick(rt, gc, tick++, policy);
    }

    for (auto* creature : creatures) {
        nwk::objects().destroy(creature->handle());
    }
    gc->set_config(saved_cfg);
}
BENCHMARK(BM_propset_gc_tick_dense_mutation)->Arg(128)->Arg(512);

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
