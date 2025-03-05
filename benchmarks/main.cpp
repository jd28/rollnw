#include <nw/formats/StaticTwoDA.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/script/Nss.hpp>
#include <nw/scriptapi.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>

#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/rules.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>

#include <benchmark/benchmark.h>
#include <mimalloc-new-delete.h>
#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>

// Note the resources loaded here should be default NWN resources distributed in the game install
// files.. for now.

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

using namespace std::literals;

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
// BENCHMARK(BM_load_module);

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
BENCHMARK(BM_creature_attack_bonus);
BENCHMARK(BM_creature_attack_roll);

BENCHMARK(BM_script_lex);
BENCHMARK(BM_script_parse);
BENCHMARK(BM_script_resolve);

// BENCHMARK(BM_model_parse);

BENCHMARK(BM_rules_master_feat);
BENCHMARK(BM_rules_modifier);

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

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
