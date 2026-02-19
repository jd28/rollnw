#include <nw/formats/StaticTwoDA.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/script/Nss.hpp>
#include <nw/scriptapi.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>

#include <nw/functions.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/Item.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/rules.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>

#include <random>

// Note the resources loaded here should be default NWN resources distributed in the game install
// files.. for now.

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

using namespace std::literals;

static fs::path resolve_stdlib_module_path(const char* argv0, std::string_view module)
{
    std::error_code ec;

    if (argv0 && argv0[0] != '\0') {
        fs::path exe_path = fs::absolute(fs::path(argv0), ec);
        if (!ec) {
            fs::path exe_candidate = fs::weakly_canonical(exe_path, ec).parent_path() / "stdlib" / module;
            ec.clear();
            if (fs::exists(exe_candidate, ec) && fs::is_directory(exe_candidate, ec)) {
                return exe_candidate;
            }
        }
        ec.clear();
    }

    fs::path cwd_candidate = fs::path{"stdlib"} / module;
    if (fs::exists(cwd_candidate, ec) && fs::is_directory(cwd_candidate, ec)) {
        return cwd_candidate;
    }

    return cwd_candidate;
}

static void BM_parse_feat_2da_static(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/feat.2da");
    for (auto _ : state) {
        nw::StaticTwoDA tda{data.copy()};
        benchmark::DoNotOptimize(tda);
    }
}
BENCHMARK(BM_parse_feat_2da_static);

static void BM_parse_feat_2da(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/feat.2da");
    for (auto _ : state) {
        nw::TwoDA tda{data.copy()};
        benchmark::DoNotOptimize(tda);
    }
}
BENCHMARK(BM_parse_feat_2da);

static void BM_load_creature_gff(benchmark::State& state)
{
    for (auto _ : state) {
        nw::Gff gff{nw::ResourceData::from_file("test_data/user/development/drorry.utc")};
        benchmark::DoNotOptimize(gff);
    }
}
BENCHMARK(BM_load_creature_gff);

static void BM_creature_from_gff(benchmark::State& state)
{
    for (auto _ : state) {
        auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
        benchmark::DoNotOptimize(ent);
    }
}
BENCHMARK(BM_creature_from_gff);

static void BM_creature_from_json(benchmark::State& state)
{
    nlohmann::json j;
    for (auto _ : state) {
        auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc.json");
        benchmark::DoNotOptimize(ent);
    }
}
BENCHMARK(BM_creature_from_json);

static void BM_creature_to_json_instance(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        nlohmann::json j;
        benchmark::DoNotOptimize(nw::serialize(ent, j, nw::SerializationProfile::instance));
    }
}
BENCHMARK(BM_creature_to_json_instance);

static void BM_creature_to_json_blueprint(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        nlohmann::json j;
        benchmark::DoNotOptimize(nw::serialize(ent, j, nw::SerializationProfile::blueprint));
    }
}
BENCHMARK(BM_creature_to_json_blueprint);

static void BM_creature_to_gff_blueprint(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        auto out = serialize(ent, nw::SerializationProfile::blueprint);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_creature_to_gff_blueprint);

static void BM_creature_to_gff_instance(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        auto out = serialize(ent, nw::SerializationProfile::instance);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_creature_to_gff_instance);

static void BM_creature_modifier_simple(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ent->levels.entries[0].id = nwn1::class_type_pale_master;
    ent->levels.entries[1].id = nwn1::class_type_dragon_disciple;

    for (auto _ : state) {
        int out = 0;
        nwk::resolve_modifier(ent, nwn1::mod_type_armor_class, nwn1::ac_armor,
            [&out](int value) { out += value; });
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_modifier_complex(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ent->levels.entries[0].id = nwn1::class_type_pale_master;
    ent->levels.entries[1].id = nwn1::class_type_dragon_disciple;

    for (auto _ : state) {
        int out = 0;
        nwk::resolve_modifier(ent, nwn1::mod_type_hitpoints,
            [&out](int value) { out += value; });
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_get_skill_rank(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ent->stats.add_feat(nwn1::feat_skill_focus_discipline);
    ent->stats.add_feat(nwn1::feat_epic_skill_focus_discipline);
    for (auto _ : state) {
        auto out = nwn1::get_skill_rank(ent, nwn1::skill_discipline);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_ability_score(benchmark::State& state)
{
    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ent->stats.add_feat(nwn1::feat_epic_great_strength_1);
    ent->stats.add_feat(nwn1::feat_epic_great_strength_2);
    for (auto _ : state) {
        auto out = nwn1::get_ability_score(ent, nwn1::ability_strength, false);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_armor_class(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    auto vs = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        auto out = nwn1::calculate_ac_versus(obj, vs, false);
        benchmark::DoNotOptimize(out);
    }
}

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

static nw::smalls::Script* load_core_combat_direct_benchmark_script()
{
    return nw::kernel::runtime().load_module_from_source("bench.core_combat_direct", R"(
        import core.combat as C;

        fn resolve_attack_result(attacker: Creature, target: Creature): int {
            var data = C.resolve_attack(attacker, target);
            if (!C.attack_data_is_valid(data)) {
                return -1;
            }
            return C.attack_data_attack_result(data);
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
            benchmark::DoNotOptimize(out);
        }
    } else {
        auto* script = load_core_combat_direct_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load direct core.combat benchmark script");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
        attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
        args.push_back(attacker_value);

        auto target_value = nw::smalls::Value::make_object(target->handle());
        target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
        args.push_back(target_value);

        auto warmup = nwk::runtime().execute_script(script, "resolve_attack_result", args);
        if (!warmup.ok()) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to execute direct core.combat benchmark script");
            return;
        }

        auto* gc = nwk::runtime().gc();
        size_t iters = 0;

        for (auto _ : state) {
            auto out = nwk::runtime().execute_script(script, "resolve_attack_result", args);
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
            benchmark::DoNotOptimize(out);
        }
    } else {
        auto* script = load_core_combat_direct_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load direct core.combat benchmark script");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
        attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
        args.push_back(attacker_value);

        auto target_value = nw::smalls::Value::make_object(target->handle());
        target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
        args.push_back(target_value);

        auto warmup = nwk::runtime().execute_script(script, "resolve_attack_result", args);
        if (!warmup.ok()) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to execute direct core.combat benchmark script");
            return;
        }

        auto* gc = nwk::runtime().gc();
        size_t iters = 0;

        for (auto _ : state) {
            auto out = nwk::runtime().execute_script(script, "resolve_attack_result", args);
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
    benchmark::State& state, bool script_direct, std::string_view policy_module)
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

    if (policy_module == "core.combat"sv) {
        auto* script = nw::kernel::runtime().load_module("core.combat");
        if (!script || script->errors() != 0) {
            nwk::unload_module();
            state.SkipWithError("failed to load core.combat policy module");
            return;
        }
    }

    auto previous_policy = nwk::config().combat_policy_module();
    nwk::config().set_combat_policy_module(std::string{policy_module});

    if (!script_direct) {
        for (auto _ : state) {
            auto out = nwn1::resolve_attack(attacker, target);
            benchmark::DoNotOptimize(out);
        }
    } else {
        auto* script = load_core_combat_direct_benchmark_script();
        if (!script || script->errors() != 0) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to load direct core.combat benchmark script");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
        attacker_value.type_id = nwk::runtime().object_subtype_for_tag(attacker->handle().type);
        args.push_back(attacker_value);

        auto target_value = nw::smalls::Value::make_object(target->handle());
        target_value.type_id = nwk::runtime().object_subtype_for_tag(target->handle().type);
        args.push_back(target_value);

        auto warmup = nwk::runtime().execute_script(script, "resolve_attack_result", args);
        if (!warmup.ok()) {
            nwk::config().set_combat_policy_module(previous_policy);
            nwk::unload_module();
            state.SkipWithError("failed to execute direct core.combat benchmark script");
            return;
        }

        auto* gc = nwk::runtime().gc();
        size_t iters = 0;
        for (auto _ : state) {
            auto out = nwk::runtime().execute_script(script, "resolve_attack_result", args);
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

static void BM_creature_attack_direct_core_combat_module_context_effect_stack(benchmark::State& state)
{
    benchmark_resolve_attack_effect_stack(state, true, ""sv);
}

static void BM_creature_attack_direct_cpp_module_context_effect_stack(benchmark::State& state)
{
    benchmark_resolve_attack_effect_stack(state, false, ""sv);
}

static void BM_creature_attack_direct_cpp_module_context(benchmark::State& state)
{
    benchmark_resolve_attack_direct_module_context(state, false);
}

static void BM_creature_attack_direct_core_combat_module_context(benchmark::State& state)
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

static void BM_creature_attack_direct_core_combat_module_context_case_drorry(benchmark::State& state)
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

static void BM_creature_attack_direct_core_combat_module_context_case_dexweapfin(benchmark::State& state)
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

static void BM_creature_attack_direct_core_combat_module_context_case_drorry_magic(benchmark::State& state)
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

static void BM_script_lex(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/scratch/nwscript.nss");
    auto ctx = new nw::script::Context;
    for (auto _ : state) {
        nw::script::NssLexer lexer{data.bytes.string_view(), ctx};
        while (lexer.next().type != nw::script::NssTokenType::END)
            ;
        benchmark::DoNotOptimize(lexer);
    }
}

static void BM_script_parse(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/scratch/nwscript.nss");
    for (auto _ : state) {
        nw::script::Context ctx;
        nw::script::Nss nss(data.bytes.string_view(), &ctx, true);
        nss.parse();
        benchmark::DoNotOptimize(nss);
    }
}

static void BM_script_resolve(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/scratch/nwscript.nss");
    for (auto _ : state) {
        nw::script::Context ctx;
        nw::script::Nss nss(data.bytes.string_view(), &ctx, true);
        nss.parse();
        nss.resolve();
        benchmark::DoNotOptimize(nss);
    }
}

static void BM_model_parse(benchmark::State& state)
{
    for (auto _ : state) {
        nw::model::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
        benchmark::DoNotOptimize(mdl);
    }
}

static void BM_resources_resman_contains(benchmark::State& state)
{
    std::vector<nw::Resource> resources;
    resources.reserve(nw::kernel::resman().size());

    auto cb = [&resources](nw::Resource res) {
        resources.push_back(res);
    };

    nw::kernel::resman().visit(cb);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, resources.size() - 1);

    for (auto _ : state) {
        auto out = nw::kernel::resman().contains(resources[dist(gen)]);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_resources_resman_contains);

static void BM_rules_master_feat(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        int out = nw::kernel::sum_master_feats<int>(
            obj, nwn1::base_item_scimitar,
            nwn1::mfeat_weapon_focus, nwn1::mfeat_weapon_focus_epic);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_rules_modifier(benchmark::State& state)
{
    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    for (auto _ : state) {
        int modifier = 0;
        nw::kernel::resolve_modifier(obj, nwn1::mod_type_attack_bonus, nwn1::attack_type_onhand, nullptr,
            [&modifier](int value) { modifier += value; });
        benchmark::DoNotOptimize(modifier);
    }
}

static void BM_load_module(benchmark::State& state)
{
    for (auto _ : state) {
        auto m = nwk::load_module("test_data/user/modules/DockerDemo.mod");
        nwk::unload_module();
        benchmark::DoNotOptimize(m);
    }
}
BENCHMARK(BM_load_module);

static void BM_start_service(benchmark::State& state)
{
    for (auto _ : state) {
        nwk::services().shutdown();
        nwk::services().start();
    }
}
// BENCHMARK(BM_start_service);
BENCHMARK(BM_creature_modifier_simple);
BENCHMARK(BM_creature_modifier_complex);
BENCHMARK(BM_creature_get_skill_rank);
BENCHMARK(BM_creature_ability_score);
BENCHMARK(BM_creature_armor_class);
BENCHMARK(BM_creature_attack);
BENCHMARK(BM_creature_attack_2);
BENCHMARK(BM_creature_attack_3);
BENCHMARK(BM_creature_attack_direct_cpp_module_context);
BENCHMARK(BM_creature_attack_direct_core_combat_module_context);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_case_drorry);
BENCHMARK(BM_creature_attack_direct_core_combat_module_context_case_drorry);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_case_dexweapfin);
BENCHMARK(BM_creature_attack_direct_core_combat_module_context_case_dexweapfin);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_case_drorry_magic);
BENCHMARK(BM_creature_attack_direct_core_combat_module_context_case_drorry_magic);
BENCHMARK(BM_creature_attack_direct_core_combat_module_context_effect_stack)->Arg(0)->Arg(5)->Arg(10)->Arg(20);
BENCHMARK(BM_creature_attack_direct_cpp_module_context_effect_stack)->Arg(0)->Arg(5)->Arg(10)->Arg(20);
BENCHMARK(BM_creature_attack_bonus);
BENCHMARK(BM_creature_attack_roll);

BENCHMARK(BM_script_lex);
BENCHMARK(BM_script_parse);
BENCHMARK(BM_script_resolve);

// BENCHMARK(BM_model_parse);

BENCHMARK(BM_rules_master_feat);
BENCHMARK(BM_rules_modifier);

static void BM_itemprop_smalls_process(benchmark::State& state)
{
    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to create creature");
        return;
    }
    auto* item = nwk::objects().make<nw::Item>();
    if (!item) {
        state.SkipWithError("failed to create item");
        return;
    }

    item->baseitem = nwn1::base_item_gloves;
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(0), 2));
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(1), 2));

    // Prime the smalls lazy generator init before benchmarking
    nw::process_item_properties(creature, item, nw::EquipIndex::arms, false);
    nw::process_item_properties(creature, item, nw::EquipIndex::arms, true);

    auto* gc = nwk::runtime().gc();
    size_t iters = 0;

    for (auto _ : state) {
        nw::process_item_properties(creature, item, nw::EquipIndex::arms, false);
        nw::process_item_properties(creature, item, nw::EquipIndex::arms, true);

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

    nwk::objects().destroy(item->handle());
    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_itemprop_smalls_process);

static void BM_itemprop_cpp_generate(benchmark::State& state)
{
    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to create creature");
        return;
    }
    auto* item = nwk::objects().make<nw::Item>();
    if (!item) {
        state.SkipWithError("failed to create item");
        return;
    }

    item->baseitem = nwn1::base_item_gloves;
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(0), 2));
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(1), 2));

    for (auto _ : state) {
        for (const auto& ip : item->properties) {
            auto* eff = nw::kernel::effects().generate(ip, nw::EquipIndex::arms, item->baseitem);
            if (eff) {
                eff->handle().creator = item->handle();
                eff->handle().category = nw::EffectCategory::item;
                nw::apply_effect(creature, eff);
            }
        }
        nw::remove_effects_by(creature, item->handle());
    }

    nwk::objects().destroy(item->handle());
    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_itemprop_cpp_generate);

static void BM_kernel_object_lookup(benchmark::State& state)
{
    constexpr int num_objects = 10000;
    std::vector<nw::ObjectHandle> handles;
    handles.reserve(num_objects);
    for (int i = 0; i < num_objects; ++i) {
        auto obj = nwk::objects().load<nw::Creature>("nw_chicken"sv);
        if (!obj) { continue; }
        handles.push_back(obj->handle());
    }

    if (handles.empty()) {
        state.SkipWithError("Failed to create any test objects");
        return;
    }

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, handles.size() - 1);

    for (auto _ : state) {
        auto obj = nw::kernel::objects().get<nw::Creature>(handles[dist(rng)]);
        benchmark::DoNotOptimize(obj);
    }
}
BENCHMARK(BM_kernel_object_lookup);

int main(int argc, char** argv)
{
    nw::init_logger(argc, argv);
    nwk::config().initialize();
    nwk::services().start();
    nw::kernel::runtime().add_module_path(resolve_stdlib_module_path(argv[0], "core"));
    nw::kernel::runtime().add_module_path(resolve_stdlib_module_path(argv[0], "nwn1"));

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
