#include <nw/formats/TwoDA.hpp>
#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/serialization/GffInputArchive.hpp>
#include <toml++/toml.hpp>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>

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
    auto sel = nwn1::sel::ability(nwn1::ability_strength);

    for (auto _ : state) {
        auto out = nw::kernel::rules().select(sel, ent);
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
    stats->entries[0].id = int(*nwn1::class_type_pale_master);

    auto is_pm = nw::Requirement{{nwn1::qual::class_level(nwn1::class_type_pale_master, 1)}};

    auto pm_ac = [](flecs::entity ent) -> nw::ModifierResult {
        auto stat = ent.get<nw::LevelStats>();
        if (!stat) { return 0; }
        auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);
        return ((pm_level / 4) + 1) * 2;
    };

    auto mod = nwn1::mod::ac_natural(pm_ac, is_pm);
    auto& rules = nw::kernel::rules();

    for (auto _ : state) {
        auto out = rules.calculate<int>(mod, ent);
        benchmark::DoNotOptimize(out);
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
