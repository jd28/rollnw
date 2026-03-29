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

#include "../tests/nwn1_test_builders.hpp"
#include <benchmark/benchmark.h>
#include <fmt/format.h>

namespace nwk = nw::kernel;

using namespace std::literals;

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
        nw::resolve_attack(obj, vs, &out);
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
        nw::resolve_attack(obj, vs, &out);
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
        nw::resolve_attack(obj, vs, &out);
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

static nw::smalls::Script* load_nwn1_policy_minimal_benchmark_script()
{
    return nw::kernel::runtime().load_module_from_source("bench.nwn1_policy_minimal", R"(
        from core.combat import { AttackData, DamageResult };
        from core.types import { Damage, DamageRoll };

        fn resolve_attack(attacker: Creature, target: object): AttackData {
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

static std::string sanitize_counter_key(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '_') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    if (out.size() > 40) {
        out.resize(40);
    }
    return out;
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
        if (!nw::apply_effect(attacker, nwn1::effect_haste())
            || !nw::apply_effect(attacker, nwn1::effect_attack_modifier(nwn1::attack_type_any, 5))
            || !nw::apply_effect(target, nwn1::effect_damage_resistance(nwn1::damage_type_fire, 20, 100))
            || !nw::apply_effect(target, nwn1::effect_damage_immunity(nwn1::damage_type_fire, 30))) {
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
    if (!nw::resolve_attack(attacker, target, &warmup)) {
        nwk::config().set_combat_policy_module(previous_policy);
        nwk::unload_module();
        state.SkipWithError("failed to warm up resolve_attack");
        return;
    }

    auto* gc = nwk::runtime().gc();
    size_t iters = 0;

    for (auto _ : state) {
        nw::AttackData out{};
        bool ok = nw::resolve_attack(attacker, target, &out);
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
        if (!nw::apply_effect(attacker, nwn1::effect_attack_modifier(nwn1::attack_type_any, 1 + (i % 3)))) {
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

static void benchmark_policy_module_with_timing(benchmark::State& state, const char* module_name, nw::smalls::Script* (*loader)())
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

    auto* script = loader();
    if (!script || script->errors() != 0) {
        nwk::unload_module();
        state.SkipWithError("failed to load policy benchmark script");
        return;
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module(module_name);

    nwn1::set_combat_policy_timing_enabled(true);
    nwn1::reset_combat_policy_timing();
    nwk::runtime().set_vm_profile_enabled(true);
    nwk::runtime().reset_vm_profile();

    for (auto _ : state) {
        nw::AttackData out;
        bool ok = nw::resolve_attack(attacker, target, &out);
        int result = ok ? static_cast<int>(out.result) : -1;
        benchmark::DoNotOptimize(result);
    }

    auto timing = nwn1::combat_policy_timing_snapshot();
    nwn1::set_combat_policy_timing_enabled(false);

    if (timing.iterations > 0) {
        auto iterations = static_cast<double>(timing.iterations);
        state.counters["policy_call_ns"] = static_cast<double>(timing.policy_call_ns) / iterations;
        state.counters["decode_ns"] = static_cast<double>(timing.decode_ns) / iterations;
        state.counters["fallback_ns"] = static_cast<double>(timing.fallback_ns) / iterations;
    }

    nwk::config().set_combat_policy_module(previous_policy);
    nwk::unload_module();
}

static void benchmark_policy_module_with_resolve_timing(benchmark::State& state, const char* module_name, nw::smalls::Script* (*loader)())
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

    auto* script = loader();
    if (!script || script->errors() != 0) {
        nwk::unload_module();
        state.SkipWithError("failed to load policy benchmark script");
        return;
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module(module_name);

    nwn1::set_combat_policy_timing_enabled(true);
    nwn1::reset_combat_policy_timing();
    nwn1::set_combat_resolve_timing_enabled(true);
    nwn1::reset_combat_resolve_timing();
    nwk::runtime().set_vm_profile_enabled(true);
    nwk::runtime().set_vm_profile_timing_enabled(false);
    nwk::runtime().reset_vm_profile();

    for (auto _ : state) {
        nw::AttackData out;
        bool ok = nw::resolve_attack(attacker, target, &out);
        int result = ok ? static_cast<int>(out.result) : -1;
        benchmark::DoNotOptimize(result);
    }

    auto policy_timing = nwn1::combat_policy_timing_snapshot();
    auto vm_profile = nwk::runtime().vm_profile_snapshot();
    auto timing = nwn1::combat_resolve_timing_snapshot();
    nwn1::set_combat_policy_timing_enabled(false);
    nwn1::set_combat_resolve_timing_enabled(false);
    nwk::runtime().set_vm_profile_enabled(false);

    auto iterations = static_cast<double>(timing.iterations ? timing.iterations : 1);
    state.counters["policy_iterations"] = static_cast<double>(policy_timing.iterations);
    state.counters["timing_iterations"] = static_cast<double>(timing.iterations);
    state.counters["prepare_ns"] = static_cast<double>(timing.prepare_ns) / iterations;
    state.counters["policy_call_ns"] = static_cast<double>(timing.policy_call_ns) / iterations;
    state.counters["decode_ns"] = static_cast<double>(timing.decode_ns) / iterations;
    state.counters["marshal_ns"] = static_cast<double>(timing.marshal_ns) / iterations;
    state.counters["total_ns"] = static_cast<double>(timing.total_ns) / iterations;

    state.counters["vm_instr_count"] = static_cast<double>(vm_profile.instruction_count);
    state.counters["vm_native_calls"] = static_cast<double>(vm_profile.native_call_count);
    state.counters["vm_native_ns"] = static_cast<double>(vm_profile.native_call_ns) / iterations;
    state.counters["vm_propset_get_calls"] = static_cast<double>(vm_profile.propset_get_or_create_count);
    state.counters["vm_propset_get_ns"] = static_cast<double>(vm_profile.propset_get_or_create_ns) / iterations;
    state.counters["vm_propset_write_calls"] = static_cast<double>(vm_profile.propset_write_field_count);
    state.counters["vm_propset_write_ns"] = static_cast<double>(vm_profile.propset_write_field_ns) / iterations;
    state.counters["vm_pop_cre_calls"] = static_cast<double>(vm_profile.populate_creature_propsets_count);
    state.counters["vm_pop_cre_ns"] = static_cast<double>(vm_profile.populate_creature_propsets_ns) / iterations;

    for (size_t i = 0; i < vm_profile.opcodes.size() && i < 10; ++i) {
        const auto& op = vm_profile.opcodes[i];
        state.counters[fmt::format("vm_op{}_id", i)] = static_cast<double>(op.opcode);
        state.counters[fmt::format("vm_op{}_count", i)] = static_cast<double>(op.count);
        state.counters[fmt::format("vm_op{}_ns", i)] = static_cast<double>(op.ns) / iterations;
    }

    for (size_t i = 0; i < vm_profile.natives.size() && i < 10; ++i) {
        const auto& native = vm_profile.natives[i];
        auto key = sanitize_counter_key(native.name);
        state.counters[fmt::format("vm_nat{}_{}_count", i, key)] = static_cast<double>(native.count);
        state.counters[fmt::format("vm_nat{}_{}_ns", i, key)] = static_cast<double>(native.ns) / iterations;
    }

    for (size_t i = 0; i < vm_profile.script_calls.size() && i < 10; ++i) {
        const auto& call = vm_profile.script_calls[i];
        auto caller_key = sanitize_counter_key(call.caller);
        auto callee_key = sanitize_counter_key(call.callee);
        state.counters[fmt::format("vm_call{}_{}_{}_count", i, caller_key, callee_key)] = static_cast<double>(call.count);
    }

    for (size_t i = 0; i < vm_profile.propset_writes.size() && i < 10; ++i) {
        const auto& write = vm_profile.propset_writes[i];
        auto key = sanitize_counter_key(write.site);
        state.counters[fmt::format("vm_psw{}_{}_count", i, key)] = static_cast<double>(write.count);
    }

    nwk::config().set_combat_policy_module(previous_policy);
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

static void BM_creature_attack_policy_minimal_timing_split(benchmark::State& state)
{
    benchmark_policy_module_with_timing(state, "bench.nwn1_policy_minimal", &load_nwn1_policy_minimal_benchmark_script);
}

static void BM_creature_attack_policy_full_timing_split(benchmark::State& state)
{
    benchmark_policy_module_with_timing(state, "bench.nwn1_policy_full", &load_nwn1_policy_full_timing_benchmark_script);
}

static void BM_creature_attack_policy_full_resolve_timing_split(benchmark::State& state)
{
    benchmark_policy_module_with_resolve_timing(state, "bench.nwn1_policy_full", &load_nwn1_policy_full_timing_benchmark_script);
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
BENCHMARK(BM_creature_attack_policy_minimal_timing_split);
BENCHMARK(BM_creature_attack_policy_full_timing_split);
BENCHMARK(BM_creature_attack_policy_full_resolve_timing_split);
