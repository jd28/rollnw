#include <nw/formats/TwoDA.hpp>
#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/serialization/GffInputArchive.hpp>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <nowide/cstdlib.hpp>

#include <fstream>

static void BM_parse_feat_2da(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("../tests/test_data/user/development/feat.2da");
    for (auto _ : state) {
        nw::TwoDA tda{ba};
        benchmark::DoNotOptimize(tda);
    }
}

static void BM_creature_from_gff(benchmark::State& state)
{
    for (auto _ : state) {
        nw::ByteArray ba = nw::ByteArray::from_file("../tests/test_data/user/development/pl_agent_001.utc");
        nw::GffInputArchive in{ba};
        nw::Creature cre{in.toplevel(), nw::SerializationProfile::blueprint};
        benchmark::DoNotOptimize(cre);
    }
}

static void BM_creature_from_json(benchmark::State& state)
{
    nlohmann::json j;
    for (auto _ : state) {
        std::ifstream f{"../tests/test_data/user/development/pl_agent_001.utc.json", std::ifstream::binary};
        j = nlohmann::json::parse(f);
        nw::Creature cre{j, nw::SerializationProfile::blueprint};
        benchmark::DoNotOptimize(cre);
    }
}

static void BM_creature_to_json(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("../tests/test_data/user/development/pl_agent_001.utc");
    nw::GffInputArchive in{ba};
    nw::Creature cre{in.toplevel(), nw::SerializationProfile::blueprint};
    for (auto _ : state) {
        benchmark::DoNotOptimize(cre.to_json(nw::SerializationProfile::instance));
    }
}

static void BM_creature_to_gff(benchmark::State& state)
{
    nw::ByteArray ba = nw::ByteArray::from_file("../tests/test_data/user/development/pl_agent_001.utc");
    nw::GffInputArchive in{ba};
    nw::Creature cre{in.toplevel(), nw::SerializationProfile::blueprint};
    for (auto _ : state) {
        auto out = cre.to_gff(nw::SerializationProfile::blueprint);
        benchmark::DoNotOptimize(out);
    }
}

BENCHMARK(BM_parse_feat_2da);
BENCHMARK(BM_creature_from_gff);
BENCHMARK(BM_creature_from_json);
BENCHMARK(BM_creature_to_gff);
BENCHMARK(BM_creature_to_json);

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

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
