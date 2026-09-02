#include "appearance_catalog.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <vector>

namespace {

nw::toolset::AppearanceCatalog make_catalog(
    benchmark::State& state, nw::toolset::AppearanceCatalogKind kind)
{
    auto module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load appearance catalog benchmark module");
        return {};
    }

    nw::toolset::AppearanceCatalog catalog;
    if (!nw::toolset::build_appearance_catalog(kind, catalog)) {
        state.SkipWithError(catalog.diagnostic.c_str());
    }
    return catalog;
}

void BM_appearance_catalog_build(benchmark::State& state)
{
    const auto kind = static_cast<nw::toolset::AppearanceCatalogKind>(state.range(0));
    auto module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load appearance catalog benchmark module");
        return;
    }

    nw::toolset::AppearanceCatalog catalog;
    for (auto _ : state) {
        bool built = nw::toolset::build_appearance_catalog(kind, catalog);
        benchmark::DoNotOptimize(built);
        benchmark::DoNotOptimize(catalog.rows.data());
        benchmark::ClobberMemory();
    }
    state.counters["source_rows"] = static_cast<double>(catalog.source_row_count);
    state.counters["valid_rows"] = static_cast<double>(catalog.rows.size());
    state.counters["catalog_bytes"] = static_cast<double>(catalog.data_bytes());
}

void BM_appearance_catalog_filter(benchmark::State& state)
{
    const auto kind = static_cast<nw::toolset::AppearanceCatalogKind>(state.range(0));
    const auto catalog = make_catalog(state, kind);
    if (state.skipped()) {
        return;
    }

    const std::string_view query = state.range(1) == 0 ? "" : "human";
    std::vector<uint32_t> matches;
    matches.reserve(catalog.rows.size());
    for (auto _ : state) {
        nw::toolset::filter_appearance_catalog(catalog, query, matches);
        benchmark::DoNotOptimize(matches.data());
        benchmark::ClobberMemory();
    }
    state.counters["catalog_rows"] = static_cast<double>(catalog.rows.size());
    state.counters["matches"] = static_cast<double>(matches.size());
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(catalog.rows.size()));
}

BENCHMARK(BM_appearance_catalog_build)
    ->Arg(static_cast<int64_t>(nw::toolset::AppearanceCatalogKind::creature))
    ->Arg(static_cast<int64_t>(nw::toolset::AppearanceCatalogKind::placeable));
BENCHMARK(BM_appearance_catalog_filter)
    ->Args({static_cast<int64_t>(nw::toolset::AppearanceCatalogKind::creature), 0})
    ->Args({static_cast<int64_t>(nw::toolset::AppearanceCatalogKind::creature), 1})
    ->Args({static_cast<int64_t>(nw::toolset::AppearanceCatalogKind::placeable), 0})
    ->Args({static_cast<int64_t>(nw::toolset::AppearanceCatalogKind::placeable), 1});

void BM_creature_body_part_popup(benchmark::State& state)
{
    auto module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module) {
        state.SkipWithError("failed to load body-part popup benchmark module");
        return;
    }

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    if (!creature) {
        nw::kernel::unload_module();
        state.SkipWithError("failed to load body-part popup benchmark creature");
        return;
    }

    auto& runtime = nw::kernel::runtime();
    auto* script = runtime.load_module_from_source("bench.creature_body_part_popup", R"(
        import core.array as Array;
        import nwn1.creature as CreatureRules;
        import nwn1.creature_state as CreatureState;

        fn option_count(target: Creature): int {
            return Array.len(CreatureRules.get_body_part_options(
                target, CreatureState.body_part_head));
        }
    )");
    if (!script || script->errors() != 0) {
        nw::kernel::objects().destroy(creature->handle());
        nw::kernel::unload_module();
        state.SkipWithError("failed to compile body-part popup benchmark script");
        return;
    }

    nw::Vector<nw::smalls::Value> args{
        nwn1::bridge::make_object_arg(creature->handle()),
    };
    runtime.reset_vm_profile();
    runtime.set_vm_profile_enabled(true);
    const auto profiled = runtime.execute_script(script, "option_count", args);
    runtime.set_vm_profile_enabled(false);
    if (!profiled.ok()) {
        nw::kernel::objects().destroy(creature->handle());
        nw::kernel::unload_module();
        state.SkipWithError(profiled.error_message.c_str());
        return;
    }

    const auto profile = runtime.vm_profile_snapshot();
    const auto resource_probe = std::ranges::find_if(profile.natives, [](const auto& entry) {
        return entry.name.ends_with("resource_exists");
    });
    state.counters["resource_probes"] = resource_probe == profile.natives.end()
        ? 0.0
        : static_cast<double>(resource_probe->count);
    state.counters["options"] = static_cast<double>(profiled.value.data.ival);

    for (auto _ : state) {
        const auto result = runtime.execute_script(script, "option_count", args);
        if (!result.ok()) {
            state.SkipWithError(result.error_message.c_str());
            break;
        }
        auto option_count = result.value.data.ival;
        benchmark::DoNotOptimize(option_count);
    }

    nw::kernel::objects().destroy(creature->handle());
    nw::kernel::unload_module();
}
BENCHMARK(BM_creature_body_part_popup);

} // namespace
