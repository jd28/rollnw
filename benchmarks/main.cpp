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
    auto sel = nw::select::ability(nwn1::ability_strength);

    for (auto _ : state) {
        auto out = nw::kernel::rules().select(sel, ent);
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

int main(int argc, char** argv)
{
    bool probe = false;
    const char* var = nowide::getenv("LIBNW_TEST_PROBE_INSTALL");
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
