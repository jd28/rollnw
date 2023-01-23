#include <nw/formats/TwoDA.hpp>
#include <nw/functions.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/legacy/Gff.hpp>
#include <nw/legacy/Tlk.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/script/Nss.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/combat.hpp>
#include <nwn1/effects.hpp>
#include <nwn1/functions.hpp>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>
#include <toml++/toml.hpp>

#include <fstream>

// Note the resources loaded here should be default NWN resources distributed in the game install
// files.. for now.

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

static void BM_parse_feat_2da(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("test_data/user/development/feat.2da");
    for (auto _ : state) {
        nw::TwoDA tda{ba};
        benchmark::DoNotOptimize(tda);
    }
}

static void BM_parse_settings_tml(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("test_data/user/settings.tml");
    for (auto _ : state) {
        auto out = toml::parse(ba.string_view());
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_deserialize(benchmark::State& state)
{
    for (auto _ : state) {
        auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
        benchmark::DoNotOptimize(ent);
    }
}

static void BM_creature_from_json(benchmark::State& state)
{
    nlohmann::json j;
    for (auto _ : state) {
        auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc.json"));
        benchmark::DoNotOptimize(ent);
    }
}

static void BM_creature_to_json(benchmark::State& state)
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        nlohmann::json j;
        benchmark::DoNotOptimize(nw::Creature::serialize(ent, j, nw::SerializationProfile::instance));
    }
}

static void BM_creature_serialize(benchmark::State& state)
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        auto out = serialize(ent, nw::SerializationProfile::instance);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_select(benchmark::State& state)
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto sel = nwn1::sel::ability(nwn1::ability_strength);

    for (auto _ : state) {
        auto out = nwk::rules().select(sel, ent);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_select2(benchmark::State& state)
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto sel = nwn1::sel::ability(nwn1::ability_strength);

    for (auto _ : state) {
        auto out = nwn1::selector(sel, ent);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_modifier_simple(benchmark::State& state)
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
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
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
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
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    ent->stats.add_feat(nwn1::feat_skill_focus_discipline);
    ent->stats.add_feat(nwn1::feat_epic_skill_focus_discipline);
    for (auto _ : state) {
        auto out = nwn1::get_skill_rank(ent, nwn1::skill_discipline);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_ability_score(benchmark::State& state)
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    ent->stats.add_feat(nwn1::feat_epic_great_strength_1);
    ent->stats.add_feat(nwn1::feat_epic_great_strength_2);
    for (auto _ : state) {
        auto out = nwn1::get_ability_score(ent, nwn1::ability_strength, false);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_armor_class(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        auto out = nwn1::calculate_ac_versus(obj, vs, false);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        auto out = nwn1::resolve_attack(obj, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_2(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/rangerdexranged.utc"));
    auto vs = nwk::objects().load<nw::Creature>("nw_drggreen003"sv);
    for (auto _ : state) {
        auto out = nwn1::resolve_attack(obj, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_3(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/dexweapfin.utc"));
    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        auto out = nwn1::resolve_attack(obj, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_bonus(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        auto out = nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_attack_roll(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    auto eff = nwn1::effect_concealment(20);
    nw::apply_effect(vs, eff);
    for (auto _ : state) {
        auto out = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, vs);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_formats_nss(benchmark::State& state)
{
    for (auto _ : state) {
        nw::script::Nss nss(fs::path("test_data/user/scratch/nwscript.nss"));
        nss.parse();
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
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        int out = nw::kernel::sum_master_feats<int>(
            obj, nwn1::base_item_scimitar,
            nwn1::mfeat_weapon_focus, nwn1::mfeat_weapon_focus_epic);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_rules_modifier(benchmark::State& state)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    for (auto _ : state) {
        int modifier = 0;
        nw::kernel::resolve_modifier(obj, nwn1::mod_type_attack_bonus, nwn1::attack_type_onhand, nullptr,
            [&modifier](int value) { modifier += value; });
        benchmark::DoNotOptimize(modifier);
    }
}

BENCHMARK(BM_parse_feat_2da);
BENCHMARK(BM_parse_settings_tml);
BENCHMARK(BM_creature_deserialize);
BENCHMARK(BM_creature_from_json);
BENCHMARK(BM_creature_serialize);
BENCHMARK(BM_creature_to_json);
BENCHMARK(BM_creature_select);
BENCHMARK(BM_creature_select2);
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

BENCHMARK(BM_formats_nss);
BENCHMARK(BM_model_parse);

BENCHMARK(BM_rules_master_feat);
BENCHMARK(BM_rules_modifier);

int main(int argc, char** argv)
{
    bool probe = false;
    const char* var = nowide::getenv("ROLLNW_TEST_PROBE_INSTALL");
    if (var) {
        if (auto val = nw::string::from<bool>(var)) {
            probe = *val;
        }
    }

    nw::init_logger(argc, argv);

    if (probe) {
        auto info = nw::probe_nwn_install();
        nwk::config().initialize({
            info.version,
            info.install,
            info.user,
        });
    } else {
        nwk::config().initialize({
            nw::GameVersion::vEE,
            "test_data/root/",
            "test_data/user/",
        });
    }

    nwk::services().start();
    nwk::load_profile(new nwn1::Profile);

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
