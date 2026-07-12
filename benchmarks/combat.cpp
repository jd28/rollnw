#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Memory.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/rules.hpp>
#include <nw/rules/combat_scheduler.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include "../tests/nwn1_test_builders.hpp"
#include <benchmark/benchmark.h>
#include <fmt/format.h>

namespace nwk = nw::kernel;

using namespace std::literals;

namespace {

bool apply_benchmark_effect(nw::ObjectBase* target, nw::Effect* effect)
{
    return target && effect && nw::kernel::effects().apply_to(target, effect);
}

} // namespace

static void BM_creature_attack(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    if (!obj || !vs) {
        state.SkipWithError("failed to load BM_creature_attack creatures");
        return;
    }
    for (auto _ : state) {
        nw::AttackData out;
        nw::combat::resolve_attack(obj, vs, &out);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_2(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/rangerdexranged.utc");
    auto vs = nwk::objects().load<nw::Creature>(nw::Resref("nw_drggreen003"));
    if (!obj || !vs) {
        state.SkipWithError("failed to load BM_creature_attack_2 creatures");
        return;
    }
    for (auto _ : state) {
        nw::AttackData out;
        nw::combat::resolve_attack(obj, vs, &out);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_3(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/dexweapfin.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    if (!obj || !vs) {
        state.SkipWithError("failed to load BM_creature_attack_3 creatures");
        return;
    }
    for (auto _ : state) {
        nw::AttackData out;
        nw::combat::resolve_attack(obj, vs, &out);
        benchmark::DoNotOptimize(out);
    }
}

static nw::smalls::Script* load_nwn1_combat_direct_benchmark_script()
{
    static uint64_t script_id = 0;
    auto module_name = std::string("bench.nwn1_combat_direct_") + std::to_string(++script_id);
    return nw::kernel::runtime().load_module_from_source(module_name, R"(
        import nwn1.combat as Combat;

        fn bench_resolve_attack_result(attacker: Creature, target: Creature): int {
            return Combat.resolve_attack_outcome(attacker, target);
        }
    )");
}

static nw::smalls::Script* load_nwn1_combat_decompose_benchmark_script()
{
    static uint64_t script_id = 0;
    auto module_name = std::string("bench.nwn1_combat_decompose_") + std::to_string(++script_id);
    return nw::kernel::runtime().load_module_from_source(module_name, R"(
        from core.combat import { AttackData, DamageResult };
        from core.types import { Damage, DamageRoll };
        from nwn1.constants import { attack_type_onhand };
        import nwn1.combat as Combat;

        fn bench_scalar(attacker: Creature, target: Creature): int {
            return 1;
        }

        fn bench_attack_minimal(attacker: Creature, target: object): AttackData {
            var base_damage: DamageResult = {
                damage_type = Damage(-1),
                amount = 0,
                unblocked = 0,
                immunity = 0,
                reduction = 0,
                reduction_remaining = 0,
                resist = 0,
                resist_remaining = 0,
            };
            var damages: array!(DamageResult);
            var rolls: array!(DamageRoll);

            return AttackData {
                attack_type = 0,
                attack_result = 2,
                attack_roll = 0,
                attack_bonus = 0,
                armor_class = 10,
                nth_attack = 0,
                damage_total = 0,
                critical_multiplier = 0,
                critical_threat = 20,
                concealment = 0,
                iteration_penalty = 0,
                is_ranged = false,
                target_is_creature = true,
                base_damage = base_damage,
                damages = damages,
                rolls = rolls,
            };
        }

        fn bench_attack_full(attacker: Creature, target: object): AttackData {
            return Combat.resolve_attack(attacker, target);
        }

        fn bench_attack_outcome(attacker: Creature, target: object): int {
            return Combat.resolve_attack_outcome(attacker, target);
        }

        fn bench_attack_bonus(attacker: Creature, target: object): int {
            var attack_type = Combat.resolve_attack_type(attacker);
            return Combat.resolve_attack_bonus(attacker, attack_type, target);
        }

        fn bench_attack_roll(attacker: Creature, target: object): int {
            var attack_type = Combat.resolve_attack_type(attacker);
            return Combat.resolve_attack_roll(attacker, attack_type, target);
        }

        fn bench_base_damage_result(attacker: Creature, target: object): DamageResult {
            return Combat.resolve_attack_base_damage_result(attacker, target, attack_type_onhand, 1);
        }
    )");
}

static nw::smalls::Script* load_nwn1_policy_full_timing_benchmark_script()
{
    return nw::kernel::runtime().load_module_from_source("bench.nwn1_policy_full", R"(
        from core.combat import { AttackData };
        import nwn1.combat as Combat;

        fn resolve_attack(attacker: Creature, target: object): AttackData {
            return Combat.resolve_attack(attacker, target);
        }
    )");
}

static nw::Vector<nw::smalls::Value> make_creature_script_args(nw::Creature* attacker, nw::Creature* target)
{
    nw::Vector<nw::smalls::Value> args;

    auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
    attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
    args.push_back(attacker_value);

    auto target_value = nw::smalls::Value::make_object(target->handle());
    target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
    args.push_back(target_value);

    return args;
}

static void benchmark_resolve_attack_direct_module_context(benchmark::State& state)
{
    static constexpr std::string_view default_attacker = "test_data/user/development/drorry.utc";
    static constexpr std::string_view default_target = "test_data/user/development/drorry.utc";

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

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static void benchmark_resolve_attack_direct_module_context_case(
    benchmark::State& state,
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
        if (!apply_benchmark_effect(attacker, nwn1::effect_haste())
            || !apply_benchmark_effect(attacker, nwn1::effect_attack_modifier(nwn1::attack_type_any, 5))
            || !apply_benchmark_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 20, 100))
            || !apply_benchmark_effect(target, nwn1::effect_damage_immunity(nwn1::damage_type_fire, 30))) {
            nwk::unload_module();
            state.SkipWithError("failed to apply benchmark magic profile effects");
            return;
        }
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module("");

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

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static void benchmark_resolve_attack_policy_toggle(benchmark::State& state, bool enable_policy_full)
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

    auto previous_policy = nwk::config().combat_policy_module();
    if (enable_policy_full) {
        auto* script = load_nwn1_policy_full_timing_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load policy_full module script");
            return;
        }
        nwk::config().set_combat_policy_module("bench.nwn1_policy_full");
    } else {
        auto* script = nwk::runtime().load_module("nwn1.combat");
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load nwn1.combat module script");
            return;
        }
        nwk::config().set_combat_policy_module("nwn1.combat");
    }

    nw::AttackData warmup{};
    if (!nw::combat::resolve_attack(attacker, target, &warmup)) {
        nwk::config().set_combat_policy_module(previous_policy);
        nwk::unload_module();
        state.SkipWithError("failed to warm up resolve_attack");
        return;
    }

    auto* gc = nwk::runtime().gc();
    size_t iters = 0;

    for (auto _ : state) {
        nw::AttackData out{};
        bool ok = nw::combat::resolve_attack(attacker, target, &out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out.result);

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

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static bool apply_effect_stack_profile(nw::Creature* attacker, nw::Creature* target, int count)
{
    if (!attacker || !target || count <= 0) {
        return true;
    }

    for (int i = 0; i < count; ++i) {
        if (!apply_benchmark_effect(attacker, nwn1::effect_attack_modifier(nwn1::attack_type_any, 1 + (i % 3)))) {
            return false;
        }
        if (!apply_benchmark_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 5 + (i % 4), 0))) {
            return false;
        }
        if (!apply_benchmark_effect(target, nwn1::effect_damage_immunity(nwn1::damage_type_fire, 2 + (i % 4)))) {
            return false;
        }
    }

    return true;
}

static nw::Effect* make_callback_bench_effect(int mode, int index)
{
    if (mode == 0) {
        auto* effect = nw::kernel::effects().create(nwn1::effect_type_miss_chance);
        if (effect) {
            effect->set_int(0, 10 + (index % 20));
        }
        return effect;
    }

    if (mode == 1) {
        if ((index % 4) == 0) {
            return nwn1::effect_haste();
        }

        auto* effect = nw::kernel::effects().create(nwn1::effect_type_miss_chance);
        if (effect) {
            effect->set_int(0, 10 + (index % 20));
        }
        return effect;
    }

    if ((index % 2) == 0) {
        return nwn1::effect_haste();
    }

    auto* effect = nwn1::effect_hitpoints_temporary(5 + (index % 6));
    if (effect) {
        return effect;
    }

    auto* fallback = nw::kernel::effects().create(nwn1::effect_type_miss_chance);
    if (fallback) {
        fallback->set_int(0, 10 + (index % 20));
    }
    return fallback;
}

static void benchmark_effect_batch_callback_pipeline(benchmark::State& state)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load benchmark module");
        return;
    }

    auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/test_creature.utc");
    if (!target) {
        nwk::unload_module();
        state.SkipWithError("failed to load benchmark creature");
        return;
    }

    const int effect_count = static_cast<int>(state.range(0));
    const int mode = static_cast<int>(state.range(1)); // 0=miss_chance only, 1=sparse callbacks, 2=dense callbacks
    auto& effects = nwk::effects();

    effects.set_callback_timing_enabled(true);
    effects.reset_callback_timing();

    for (auto _ : state) {
        nw::Vector<nw::Effect*> to_apply;
        to_apply.reserve(effect_count);
        for (int i = 0; i < effect_count; ++i) {
            auto* effect = make_callback_bench_effect(mode, i);
            if (!effect) {
                continue;
            }
            to_apply.push_back(effect);
        }

        nw::Vector<nw::Effect*> apply_failed;
        auto applied = effects.apply_to(target, to_apply, &apply_failed);
        if (applied != to_apply.size() || !apply_failed.empty()) {
            state.SkipWithError("effect apply_to failed during callback benchmark");
            for (auto* effect : to_apply) {
                if (effect) {
                    effects.destroy(effect);
                }
            }
            break;
        }

        nw::Vector<nw::Effect*> remove_failed;
        auto removed = effects.remove_from(target, to_apply, true, &remove_failed);
        if (removed != to_apply.size() || !remove_failed.empty()) {
            state.SkipWithError("effect remove_from failed during callback benchmark");
            break;
        }
    }

    const auto timing = effects.callback_timing_stats();
    const auto batches = static_cast<double>(timing.batches == 0 ? 1 : timing.batches);
    const auto iterations = static_cast<double>(state.iterations() == 0 ? 1 : state.iterations());

    state.counters["callback_batches"] = static_cast<double>(timing.batches);
    state.counters["effects_scanned"] = static_cast<double>(timing.effects_scanned);
    state.counters["effects_dispatched"] = static_cast<double>(timing.effects_dispatched);
    state.counters["filter_ns_per_batch"] = static_cast<double>(timing.filter_ns) / batches;
    state.counters["marshal_ns_per_batch"] = static_cast<double>(timing.marshal_ns) / batches;
    state.counters["dispatch_ns_per_batch"] = static_cast<double>(timing.dispatch_ns) / batches;
    state.counters["scanned_per_iter"] = static_cast<double>(timing.effects_scanned) / iterations;
    state.counters["dispatched_per_iter"] = static_cast<double>(timing.effects_dispatched) / iterations;

    effects.set_callback_timing_enabled(false);
    nwk::unload_module();
}

static void benchmark_resolve_attack_effect_stack(benchmark::State& state)
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

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static void BM_creature_attack_direct_nwn1_combat_module_context_effect_stack(benchmark::State& state)
{
    benchmark_resolve_attack_effect_stack(state);
}

static void BM_effect_batch_callback_pipeline(benchmark::State& state)
{
    benchmark_effect_batch_callback_pipeline(state);
}

static void BM_creature_attack_direct_nwn1_combat_module_context(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context(state);
}

static void BM_creature_attack_direct_nwn1_combat_module_context_case_drorry(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        "test_data/user/development/drorry.utc"sv,
        "test_data/user/development/drorry.utc"sv);
}

static void BM_creature_attack_direct_nwn1_combat_module_context_case_dexweapfin(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        "test_data/user/development/dexweapfin.utc"sv,
        "test_data/user/development/drorry.utc"sv);
}

static void BM_creature_attack_direct_nwn1_combat_module_context_case_drorry_magic(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context_case(
        state,
        "test_data/user/development/drorry.utc"sv,
        "test_data/user/development/drorry.utc"sv,
        true);
}

static void BM_creature_attack_ab_policy_off(benchmark::State& state)
{
    benchmark_resolve_attack_policy_toggle(state, false);
}

static void BM_creature_attack_ab_policy_full(benchmark::State& state)
{
    benchmark_resolve_attack_policy_toggle(state, true);
}

static void benchmark_smalls_decompose(benchmark::State& state, const char* fn_name)
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

    auto* script = load_nwn1_combat_decompose_benchmark_script();
    if (!script || script->errors() != 0) {
        nwk::unload_module();
        state.SkipWithError("failed to load decompose benchmark script");
        return;
    }

    auto args = make_creature_script_args(attacker, target);
    auto warmup = nwk::runtime().execute_script(script, fn_name, args);
    if (!warmup.ok()) {
        nwk::unload_module();
        state.SkipWithError("failed to execute decompose benchmark script");
        return;
    }

    auto* gc = nwk::runtime().gc();
    size_t iters = 0;
    for (auto _ : state) {
        auto out = nwk::runtime().execute_script(script, fn_name, args);
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

    nwk::unload_module();
}

static void BM_smalls_attack_decompose_scalar(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_scalar");
}

static void BM_smalls_attack_decompose_minimal_attack_data(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_attack_minimal");
}

static void BM_smalls_attack_decompose_full_attack_data(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_attack_full");
}

static void BM_smalls_attack_phase_outcome(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_attack_outcome");
}

static void BM_smalls_attack_phase_bonus(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_attack_bonus");
}

static void BM_smalls_attack_phase_roll(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_attack_roll");
}

static void BM_smalls_attack_phase_base_damage_result(benchmark::State& state)
{
    benchmark_smalls_decompose(state, "bench_base_damage_result");
}

BENCHMARK(BM_creature_attack);
BENCHMARK(BM_creature_attack_2);
BENCHMARK(BM_creature_attack_3);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_case_drorry);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_case_dexweapfin);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_case_drorry_magic);
BENCHMARK(BM_creature_attack_direct_nwn1_combat_module_context_effect_stack)->Arg(0)->Arg(5)->Arg(10)->Arg(20);
BENCHMARK(BM_effect_batch_callback_pipeline)
    ->Args({10, 0})
    ->Args({10, 1})
    ->Args({10, 2})
    ->Args({100, 0})
    ->Args({100, 1})
    ->Args({100, 2})
    ->Args({1000, 0})
    ->Args({1000, 1})
    ->Args({1000, 2});
BENCHMARK(BM_creature_attack_ab_policy_off);
BENCHMARK(BM_creature_attack_ab_policy_full);
BENCHMARK(BM_smalls_attack_decompose_scalar);
BENCHMARK(BM_smalls_attack_decompose_minimal_attack_data);
BENCHMARK(BM_smalls_attack_decompose_full_attack_data);
BENCHMARK(BM_smalls_attack_phase_outcome);
BENCHMARK(BM_smalls_attack_phase_bonus);
BENCHMARK(BM_smalls_attack_phase_roll);
BENCHMARK(BM_smalls_attack_phase_base_damage_result);
