#include <nw/components/Creature.hpp>
#include <nw/formats/Nss.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/serialization/GffInputArchive.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/functions.hpp>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>
#include <toml++/toml.hpp>

#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

static void BM_parse_feat_2da(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("../tests/test_data/user/development/placeables.2da");
    for (auto _ : state) {
        nw::TwoDA tda{ba};
        benchmark::DoNotOptimize(tda);
    }
}

static void BM_parse_settings_tml(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("../tests/test_data/user/settings.tml");
    for (auto _ : state) {
        auto out = toml::parse(ba.string_view());
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_from_gff(benchmark::State& state)
{
    for (auto _ : state) {
        auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
        benchmark::DoNotOptimize(ent);
    }
}

static void BM_creature_from_json(benchmark::State& state)
{
    nlohmann::json j;
    for (auto _ : state) {
        auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc.json"));
        benchmark::DoNotOptimize(ent);
    }
}

static void BM_creature_to_json(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    for (auto _ : state) {
        nlohmann::json j;
        benchmark::DoNotOptimize(nw::Creature::serialize(ent, j, nw::SerializationProfile::instance));
    }
}

static void BM_creature_to_gff(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    for (auto _ : state) {
        auto out = nw::Creature::serialize(ent, nw::SerializationProfile::instance);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_select(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    // auto sel = nwn1::sel::ability(nwn1::ability_strength);

    for (auto _ : state) {
        auto out = ent.get<nw::CreatureStats>(); // nw::kernel::rules().select(sel, ent);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_select2(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    auto sel = nwn1::sel::ability(nwn1::ability_strength);

    for (auto _ : state) {
        auto out = nwn1::selector(sel, ent);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_modifier(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    auto stats = ent.get_mut<nw::LevelStats>();
    stats->entries[0].id = nwn1::class_type_pale_master;
    stats->entries[1].id = nwn1::class_type_dragon_disciple;

    auto& rules = nw::kernel::rules();

    for (auto _ : state) {
        auto out = rules.calculate<int>(ent, nwn1::mod_type_armor_class, nwn1::ac_armor);
        auto res = rules.calculate<int>(ent, nwn1::mod_type_hitpoints);
        benchmark::DoNotOptimize(out);
        benchmark::DoNotOptimize(res);
    }
}

static void BM_creature_get_skill_rank(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    ent.get_mut<nw::CreatureStats>()->add_feat(nwn1::feat_skill_focus_discipline);
    ent.get_mut<nw::CreatureStats>()->add_feat(nwn1::feat_epic_skill_focus_discipline);
    for (auto _ : state) {
        auto out = nwn1::get_skill_rank(ent, nwn1::skill_discipline, false);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_creature_ability_score(benchmark::State& state)
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    ent.get_mut<nw::CreatureStats>()->add_feat(nwn1::feat_epic_great_strength_1);
    ent.get_mut<nw::CreatureStats>()->add_feat(nwn1::feat_epic_great_strength_2);
    for (auto _ : state) {
        auto out = nwn1::get_ability_score(ent, nwn1::ability_strength, false);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_formats_nss(benchmark::State& state)
{
    for (auto _ : state) {
        nw::script::Nss nss("../tests/test_data/user/scratch/nwscript.nss");
        auto prog = nss.parse();
        benchmark::DoNotOptimize(prog);
    }
}

static void BM_model_parse(benchmark::State& state)
{
    for (auto _ : state) {
        nw::Mdl mdl{"../tests/test_data/user/development/alt_dfa19.mdl"};
        benchmark::DoNotOptimize(mdl);
    }
}

BENCHMARK(BM_parse_feat_2da);
BENCHMARK(BM_parse_settings_tml);
BENCHMARK(BM_creature_from_gff);
BENCHMARK(BM_creature_from_json);
BENCHMARK(BM_creature_to_gff);
BENCHMARK(BM_creature_to_json);
BENCHMARK(BM_creature_select);
BENCHMARK(BM_creature_select2);
BENCHMARK(BM_creature_modifier);
BENCHMARK(BM_creature_get_skill_rank);
BENCHMARK(BM_creature_ability_score);

BENCHMARK(BM_formats_nss);
BENCHMARK(BM_model_parse);

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
        nw::kernel::config().initialize({
            info.version,
            info.install,
            info.user,
        });
    } else {
        nw::kernel::config().initialize({
            nw::GameVersion::vEE,
            "test_data/root/",
            "test_data/user/",
        });
    }

    nw::kernel::services().start();
    nw::kernel::load_profile(new nwn1::Profile);

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
