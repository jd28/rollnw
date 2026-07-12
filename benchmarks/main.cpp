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
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>

#include <nw/objects/Equips.hpp>
#include <nw/objects/Item.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/rules.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>

#include "../tests/item_gff_builders.hpp"

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

static void set_benchmark_working_directory(const char* argv0)
{
    std::error_code ec;
    if (!argv0 || argv0[0] == '\0') {
        return;
    }

    fs::path exe_path = fs::absolute(fs::path(argv0), ec);
    if (ec) {
        return;
    }

    fs::path exe_dir = fs::weakly_canonical(exe_path, ec).parent_path();
    if (ec) {
        return;
    }

    fs::path test_data = exe_dir / "test_data";
    fs::path stdlib = exe_dir / "stdlib";
    if (fs::exists(test_data, ec) && fs::is_directory(test_data, ec)
        && fs::exists(stdlib, ec) && fs::is_directory(stdlib, ec)) {
        ec.clear();
        fs::current_path(exe_dir, ec);
    }
}

static void BM_parse_feat_2da_static(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/feat.2da");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        nw::StaticTwoDA tda{data.copy()};
        benchmark::DoNotOptimize(tda);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}
BENCHMARK(BM_parse_feat_2da_static);

static void BM_parse_feat_2da(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/feat.2da");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        nw::TwoDA tda{data.copy()};
        benchmark::DoNotOptimize(tda);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}
BENCHMARK(BM_parse_feat_2da);

static void BM_load_creature_gff(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/drorry.utc");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        nw::Gff gff{nw::ResourceData::from_file("test_data/user/development/drorry.utc")};
        benchmark::DoNotOptimize(gff);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}
BENCHMARK(BM_load_creature_gff);

static void BM_creature_from_gff(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/drorry.utc");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
        benchmark::DoNotOptimize(ent);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}
BENCHMARK(BM_creature_from_gff);

static void BM_creature_from_json(benchmark::State& state)
{
    nlohmann::json j;
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/development/drorry.utc.json");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc.json");
        benchmark::DoNotOptimize(ent);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
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

static void BM_script_lex(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/scratch/nwscript.nss");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    auto ctx = new nw::script::Context;
    for (auto _ : state) {
        nw::script::NssLexer lexer{data.bytes.string_view(), ctx};
        while (lexer.next().type != nw::script::NssTokenType::END)
            ;
        benchmark::DoNotOptimize(lexer);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}

static void BM_script_parse(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/scratch/nwscript.nss");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        nw::script::Context ctx;
        nw::script::Nss nss(data.bytes.string_view(), &ctx, true);
        nss.parse();
        benchmark::DoNotOptimize(nss);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}

static void BM_script_resolve(benchmark::State& state)
{
    nw::ResourceData data = nw::ResourceData::from_file("test_data/user/scratch/nwscript.nss");
    const auto bytes = static_cast<int64_t>(data.bytes.size());
    for (auto _ : state) {
        nw::script::Context ctx;
        nw::script::Nss nss(data.bytes.string_view(), &ctx, true);
        nss.parse();
        nss.resolve();
        benchmark::DoNotOptimize(nss);
    }
    state.SetBytesProcessed(state.iterations() * bytes);
}

[[maybe_unused]] static void BM_model_parse(benchmark::State& state)
{
    std::error_code ec;
    const auto file_size = fs::file_size("test_data/user/development/alt_dfa19.mdl", ec);
    const auto bytes = ec ? int64_t{0} : static_cast<int64_t>(file_size);
    for (auto _ : state) {
        nw::model::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
        benchmark::DoNotOptimize(mdl);
    }
    if (bytes > 0) {
        state.SetBytesProcessed(state.iterations() * bytes);
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

static void BM_load_module(benchmark::State& state)
{
    for (auto _ : state) {
        auto m = nwk::load_module("test_data/user/modules/DockerDemo.mod");
        nwk::unload_module();
        benchmark::DoNotOptimize(m);
    }
}
BENCHMARK(BM_load_module);

[[maybe_unused]] static void BM_start_service(benchmark::State& state)
{
    for (auto _ : state) {
        nwk::services().shutdown();
        nwk::services().start();
    }
}
// BENCHMARK(BM_start_service);

BENCHMARK(BM_script_lex);
BENCHMARK(BM_script_parse);
BENCHMARK(BM_script_resolve);

// BENCHMARK(BM_model_parse);

static void BM_itemprop_smalls_process(benchmark::State& state)
{
    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to create creature");
        return;
    }
    rollnw::tests::TestItemGff spec{.base_item = nwn1::base_item_gloves};
    spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_ability_bonus),
        .subtype = static_cast<uint16_t>(*nw::Ability::make(0)),
        .cost_value = 2,
    });
    spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_ability_bonus),
        .subtype = static_cast<uint16_t>(*nw::Ability::make(1)),
        .cost_value = 2,
    });

    auto* item = rollnw::tests::make_item_from_gff(spec);
    if (!item) {
        state.SkipWithError("failed to create item");
        nwk::objects().destroy(creature->handle());
        return;
    }

    // Prime the smalls lazy generator init before benchmarking
    nwn1::process_item_properties(creature, item, nw::EquipIndex::arms, false);
    nwn1::process_item_properties(creature, item, nw::EquipIndex::arms, true);

    auto& rt = nwk::runtime();
    auto* gc = rt.gc();
    rt.clear_non_vm_owned_handle_registry();
    rt.prune_stale_handle_registry();
    size_t iters = 0;

    for (auto _ : state) {
        nwn1::process_item_properties(creature, item, nw::EquipIndex::arms, false);
        nwn1::process_item_properties(creature, item, nw::EquipIndex::arms, true);

        ++iters;
        if (gc && (iters % 1024) == 0) {
            state.PauseTiming();
            gc->collect_minor();
            rt.clear_non_vm_owned_handle_registry();
            rt.prune_stale_handle_registry();
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

static void BM_nwn1_equip_unequip_high_level(benchmark::State& state)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load benchmark module");
        return;
    }

    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/test_creature.utc");
    auto* item = nwk::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    if (!creature || !item || !creature->instantiate()) {
        nwk::unload_module();
        state.SkipWithError("failed to load/instantiate benchmark creature/item");
        return;
    }

    if (nw::get_equipped_item(creature, nw::EquipIndex::chest)) {
        nw::unequip_item_in_slot(creature, nw::EquipIndex::chest);
    }

    if (!nwn1::equip_item(creature, item, nw::EquipIndex::chest)) {
        nwk::unload_module();
        state.SkipWithError("failed to warm up equip_item benchmark");
        return;
    }

    if (nwn1::unequip_item(creature, nw::EquipIndex::chest) != item) {
        nwk::unload_module();
        state.SkipWithError("failed to warm up unequip_item benchmark");
        return;
    }

    auto& rt = nwk::runtime();
    auto* gc = rt.gc();
    rt.clear_non_vm_owned_handle_registry();
    rt.prune_stale_handle_registry();
    size_t iters = 0;

    for (auto _ : state) {
        bool equipped = nwn1::equip_item(creature, item, nw::EquipIndex::chest);
        auto* removed = nwn1::unequip_item(creature, nw::EquipIndex::chest);
        benchmark::DoNotOptimize(equipped);
        benchmark::DoNotOptimize(removed);

        if (!equipped || removed != item) {
            state.SkipWithError("equip/unequip benchmark iteration failed");
            break;
        }

        ++iters;
        if (gc && (iters % 1024) == 0) {
            state.PauseTiming();
            gc->collect_minor();
            rt.clear_non_vm_owned_handle_registry();
            rt.prune_stale_handle_registry();
            state.ResumeTiming();
        }
    }

    state.SetItemsProcessed(state.iterations() * 2);

    if (nw::get_equipped_item(creature, nw::EquipIndex::chest)) {
        nwn1::unequip_item(creature, nw::EquipIndex::chest);
    }

    if (gc) {
        gc->collect_minor();
    }

    nwk::unload_module();
}
BENCHMARK(BM_nwn1_equip_unequip_high_level);

static void BM_smalls_bridge_noop(benchmark::State& state)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load benchmark module");
        return;
    }

    auto& rt = nwk::runtime();
    auto* script = rt.load_module_from_source("bench.noop", R"(
fn noop(): int {
    return 1;
}
)");
    if (!script) {
        nwk::unload_module();
        state.SkipWithError("failed to compile bench.noop module");
        return;
    }

    nw::Vector<nw::smalls::Value> args;
    auto warmup = nwn1::bridge::call_nwn1_module_int("bench.noop", "noop", args);
    if (!warmup || *warmup != 1) {
        nwk::unload_module();
        state.SkipWithError("failed to warm up smalls bridge noop benchmark");
        return;
    }

    for (auto _ : state) {
        auto out = nwn1::bridge::call_nwn1_module_int("bench.noop", "noop", args);
        benchmark::DoNotOptimize(out);
        if (!out) {
            state.SkipWithError("smalls bridge noop call failed");
            break;
        }
    }

    nwk::unload_module();
}
BENCHMARK(BM_smalls_bridge_noop);

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
    set_benchmark_working_directory(argc > 0 ? argv[0] : nullptr);
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
