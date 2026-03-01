#include <nw/functions.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Memory.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/rules.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>
#include <nw/scriptapi.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

namespace nwk = nw::kernel;

using namespace std::literals;

static void BM_creature_attack(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        auto out = nwn1::resolve_attack(obj, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_2(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/rangerdexranged.utc");
    auto vs = nwk::objects().load<nw::Creature>(nw::Resref("nw_drggreen003"));
    for (auto _ : state) {
        auto out = nwn1::resolve_attack(obj, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_3(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/dexweapfin.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        auto out = nwn1::resolve_attack(obj, vs);
        benchmark::DoNotOptimize(out);
    }
}

static nw::smalls::Script* load_nwn1_combat_direct_benchmark_script()
{
    return nw::kernel::runtime().load_module_from_source("bench.nwn1_combat_direct", R"(
        import nwn1.combat as Combat;

        fn bench_resolve_attack_result(attacker: Creature, target: Creature): int {
            return Combat.resolve_attack_outcome(attacker, target);
        }
    )");
}

static void benchmark_resolve_attack_direct_module_context(benchmark::State& state, bool script_path)
{
    static constexpr std::string_view default_attacker = "test_data/user/development/pl_agent_001.utc";
    static constexpr std::string_view default_target = "test_data/user/development/nw_chicken.utc";

    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load benchmark module");
        return;
    }

    auto* attacker = nwk::objects().load_file<nw::Creature>(default_attacker);
    auto* target = nwk::objects().load_file<nw::Creature>(default_target);
    if (!attacker || !target) {
        nwk::unload_module();
        state.SkipWithError("failed to load direct benchmark creatures");
        return;
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module("");

    if (!script_path) {
        for (auto _ : state) {
            auto out = nwn1::resolve_attack(attacker, target);
            int result = out ? static_cast<int>(out->result) : -1;
            benchmark::DoNotOptimize(result);
        }
    } else {
        auto* script = load_nwn1_combat_direct_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load direct nwn1.combat benchmark script");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
        attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
        args.push_back(attacker_value);

        auto target_value = nw::smalls::Value::make_object(target->handle());
        target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
        args.push_back(target_value);

        auto warmup = nwk::runtime().execute_script(script, "bench_resolve_attack_result", args);
        if (!warmup.ok()) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to execute direct nwn1.combat benchmark script");
            return;
        }

        auto* gc = nwk::runtime().gc();
        size_t iters = 0;

        for (auto _ : state) {
            auto out = nwk::runtime().execute_script(script, "bench_resolve_attack_result", args);
            benchmark::DoNotOptimize(out);

            ++iters;
            if (gc && (iters % 1024) == 0) {
                state.PauseTiming();
                gc->collect_minor();
                state.ResumeTiming();
            }
        }

        if (gc) {
            gc->collect_minor();
        }
    }

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static void benchmark_resolve_attack_direct_module_context_case(
    benchmark::State& state, bool script_path,
    std::string_view attacker_path, std::string_view target_path,
    bool magic_profile = false)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load benchmark module");
        return;
    }

    auto* attacker = nwk::objects().load_file<nw::Creature>(attacker_path);
    auto* target = nwk::objects().load_file<nw::Creature>(target_path);
    if (!attacker || !target) {
        nwk::unload_module();
        state.SkipWithError("failed to load direct benchmark creatures");
        return;
    }

    if (magic_profile) {
        if (!nw::apply_effect(attacker, nwn1::effect_haste())
            || !nw::apply_effect(attacker, nwn1::effect_attack_modifier(nwn1::attack_type_any, 5))
            || !nw::apply_effect(attacker, nwn1::effect_damage_bonus(nwn1::damage_type_fire, {2, 6, 4}))
            || !nw::apply_effect(target, nwn1::effect_concealment(30))
            || !nw::apply_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 20, 100))
            || !nw::apply_effect(target, nwn1::effect_damage_immunity(nwn1::damage_type_fire, 30))) {
            nwk::unload_module();
            state.SkipWithError("failed to apply benchmark magic profile effects");
            return;
        }
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module("");

    if (!script_path) {
        for (auto _ : state) {
            auto out = nwn1::resolve_attack(attacker, target);
            int result = out ? static_cast<int>(out->result) : -1;
            benchmark::DoNotOptimize(result);
        }
    } else {
        auto* script = load_nwn1_combat_direct_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load direct nwn1.combat benchmark script");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
        attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
        args.push_back(attacker_value);

        auto target_value = nw::smalls::Value::make_object(target->handle());
        target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
        args.push_back(target_value);

        auto warmup = nwk::runtime().execute_script(script, "bench_resolve_attack_result", args);
        if (!warmup.ok()) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to execute direct nwn1.combat benchmark script");
            return;
        }

        auto* gc = nwk::runtime().gc();
        size_t iters = 0;

        for (auto _ : state) {
            auto out = nwk::runtime().execute_script(script, "bench_resolve_attack_result", args);
            benchmark::DoNotOptimize(out);

            ++iters;
            if (gc && (iters % 1024) == 0) {
                state.PauseTiming();
                gc->collect_minor();
                state.ResumeTiming();
            }
        }

        if (gc) {
            gc->collect_minor();
        }
    }

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static bool apply_effect_stack_profile(nw::Creature* attacker, nw::Creature* target, int count)
{
    if (!attacker || !target || count <= 0) {
        return true;
    }

    for (int i = 0; i < count; ++i) {
        if (!nw::apply_effect(attacker, nwn1::effect_attack_modifier(nwn1::attack_type_any, 1 + (i % 3)))) {
            return false;
        }
        if (!nw::apply_effect(attacker, nwn1::effect_damage_bonus(nwn1::damage_type_fire, {1, 4, i % 2}))) {
            return false;
        }
        if (!nw::apply_effect(target, nwn1::effect_concealment(10 + ((i % 3) * 5)))) {
            return false;
        }
        if (!nw::apply_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 5 + (i % 4), 0))) {
            return false;
        }
        if (!nw::apply_effect(target, nwn1::effect_damage_immunity(nwn1::damage_type_fire, 2 + (i % 4)))) {
            return false;
        }
    }

    return true;
}

static void benchmark_resolve_attack_effect_stack(
    benchmark::State& state, bool script_direct)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load benchmark module");
        return;
    }

    auto* attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    if (!attacker || !target) {
        nwk::unload_module();
        state.SkipWithError("failed to load benchmark creatures");
        return;
    }

    if (!apply_effect_stack_profile(attacker, target, static_cast<int>(state.range(0)))) {
        nwk::unload_module();
        state.SkipWithError("failed to apply effect stack profile");
        return;
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module("");

    if (!script_direct) {
        for (auto _ : state) {
            auto out = nwn1::resolve_attack(attacker, target);
            int result = out ? static_cast<int>(out->result) : -1;
            benchmark::DoNotOptimize(result);
        }
    } else {
        auto* script = load_nwn1_combat_direct_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load direct nwn1.combat benchmark script");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
        attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
        args.push_back(attacker_value);

        auto target_value = nw::smalls::Value::make_object(target->handle());
        target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
        args.push_back(target_value);

        auto warmup = nwk::runtime().execute_script(script, "bench_resolve_attack_result", args);
        if (!warmup.ok()) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to execute direct nwn1.combat benchmark script");
            return;
        }

        auto* gc = nwk::runtime().gc();
        size_t iters = 0;
        for (auto _ : state) {
            auto out = nwk::runtime().execute_script(script, "bench_resolve_attack_result", args);
            benchmark::DoNotOptimize(out);
            ++iters;
            if (gc && (iters % 1024) == 0) {
                state.PauseTiming();
                gc->collect_minor();
                state.ResumeTiming();
            }
        }

        if (gc) {
            gc->collect_minor();
        }
    }

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static void BM_creature_attack_direct_nwn1_combat_module_context_effect_stack(benchmark::State& state)
{
    benchmark_resolve_attack_effect_stack(state, true);
}

static void BM_creature_attack_direct_cpp_module_context_effect_stack(benchmark::State& state)
{
    benchmark_resolve_attack_effect_stack(state, false);
}

static void BM_creature_attack_direct_cpp_module_context(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context(state, false);
}

static void BM_creature_attack_direct_nwn1_combat_module_context(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context(state, true);
}

static void BM_creature_attack_direct_cpp_module_context_case_drorry(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        false,
        "test_data/user/development/drorry.utc"sv,
        "test_data/user/development/drorry.utc"sv);
}

static void BM_creature_attack_direct_nwn1_combat_module_context_case_drorry(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        true,
        "test_data/user/development/drorry.utc"sv,
        "test_data/user/development/drorry.utc"sv);
}

static void BM_creature_attack_direct_cpp_module_context_case_dexweapfin(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        false,
        "test_data/user/development/dexweapfin.utc"sv,
        "test_data/user/development/drorry.utc"sv);
}

static void BM_creature_attack_direct_nwn1_combat_module_context_case_dexweapfin(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        true,
        "test_data/user/development/dexweapfin.utc"sv,
        "test_data/user/development/drorry.utc"sv);
}

static void BM_creature_attack_direct_cpp_module_context_case_drorry_magic(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        false,
        "test_data/user/development/drorry.utc"sv,
        "test_data/user/development/drorry.utc"sv,
        true);
}

static void BM_creature_attack_direct_nwn1_combat_module_context_case_drorry_magic(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        true,
        "test_data/user/development/drorry.utc"sv,
        "test_data/user/development/drorry.utc"sv,
        true);
}

static void BM_creature_attack_bonus(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        auto out = nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_roll(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto eff = nwn1::effect_concealment(20);
    nw::apply_effect(vs, eff);
    for (auto _ : state) {
        auto out = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_damage_reduction_scan(benchmark::State& state)
{
    auto* target = nwk::objects().make<nw::Creature>();
    auto* versus = nwk::objects().make<nw::Creature>();
    if (!target || !versus) {
        state.SkipWithError("failed to create benchmark creatures");
        return;
    }

    int count = static_cast<int>(state.range(0));
    for (int i = 0; i < count; ++i) {
        nw::apply_effect(target, nwn1::effect_damage_reduction(5 + (i % 4), 10 + (i % 3), 0));
        nw::apply_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 2 + (i % 3), 0));
        nw::apply_effect(target, nwn1::effect_concealment(10 + (i % 3) * 5));
    }

    for (auto _ : state) {
        auto out = nwn1::resolve_damage_reduction(target, 10, versus);
        benchmark::DoNotOptimize(out.first);
    }

    nwk::objects().destroy(target->handle());
    nwk::objects().destroy(versus->handle());
}

static void BM_damage_resistance_scan(benchmark::State& state)
{
    auto* target = nwk::objects().make<nw::Creature>();
    auto* versus = nwk::objects().make<nw::Creature>();
    if (!target || !versus) {
        state.SkipWithError("failed to create benchmark creatures");
        return;
    }

    int count = static_cast<int>(state.range(0));
    for (int i = 0; i < count; ++i) {
        nw::apply_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 2 + (i % 4), 0));
        nw::apply_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_cold, 2 + (i % 4), 0));
        nw::apply_effect(target, nwn1::effect_damage_reduction(5 + (i % 3), 10 + (i % 2), 0));
    }

    for (auto _ : state) {
        auto out = nwn1::resolve_damage_resistance(target, nwn1::damage_type_fire, versus);
        benchmark::DoNotOptimize(out.first);
    }

    nwk::objects().destroy(target->handle());
    nwk::objects().destroy(versus->handle());
}

BENCHMARK(BM_creature_attack);
BENCHMARK(BM_creature_attack_2);
BENCHMARK(BM_creature_attack_3);
BENCHMARK(BM_creature_attack_direct_cpp_module_context);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_case_drorry);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_case_drorry);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_case_dexweapfin);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_case_dexweapfin);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_case_drorry_magic);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_case_drorry_magic);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_effect_stack)->Arg(0)->Arg(5)->Arg(10)->Arg(20);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_effect_stack)->Arg(0)->Arg(5)->Arg(10)->Arg(20);
BENCHMARK(BM_creature_attack_bonus);
BENCHMARK(BM_creature_attack_roll);
BENCHMARK(BM_damage_reduction_scan)->Arg(0)->Arg(8)->Arg(32)->Arg(128);
BENCHMARK(BM_damage_resistance_scan)->Arg(0)->Arg(8)->Arg(32)->Arg(128);
