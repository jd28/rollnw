#include "appearance_catalog.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

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
    if (!nw::toolset::build_appearance_catalog(nw::kernel::runtime(), kind, catalog)) {
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
        bool built = nw::toolset::build_appearance_catalog(nw::kernel::runtime(), kind, catalog);
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

} // namespace
