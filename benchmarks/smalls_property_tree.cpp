#include "smalls_property_tree.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <string_view>

namespace {

bool ensure_property_tree_modules()
{
    static const bool loaded = [] {
        auto& runtime = nw::kernel::runtime();
        constexpr std::array modules{
            std::string_view{"core.creature"},
            std::string_view{"core.item"},
            std::string_view{"core.encounter"},
            std::string_view{"core.trigger"},
        };
        for (const auto module : modules) {
            if (!runtime.load_module(module)) {
                return false;
            }
        }
        return true;
    }();
    return loaded;
}

template <typename Object>
Object* make_property_tree_object(benchmark::State& state)
{
    if (!ensure_property_tree_modules()) {
        state.SkipWithError("failed to load property tree Smalls modules");
        return nullptr;
    }
    auto* object = nw::kernel::objects().make<Object>();
    if (!object) {
        state.SkipWithError("failed to create property tree object");
        return nullptr;
    }
    nw::kernel::runtime().init_object_propsets(object->handle());
    return object;
}

void expand_all_aggregate_fields(const nw::toolset::PropertyTreeSnapshot& snapshot,
    nw::toolset::PropertyTreeExpansionState& expansion)
{
    for (const auto& row : snapshot.rows) {
        if (row.node_kind != nw::toolset::PropertyNodeKind::propset
            && nw::toolset::has_property_flag(row.flags, nw::toolset::PropertyNodeFlags::has_children)) {
            expansion.set_expanded(row.root_propset_type, snapshot.path(row), true, false);
        }
    }
}

template <typename Object>
void BM_smalls_property_tree_snapshot(benchmark::State& state)
{
    auto* object = make_property_tree_object<Object>(state);
    if (!object) {
        return;
    }

    auto& runtime = nw::kernel::runtime();
    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::PropertyTreeSnapshot warm;
    nw::toolset::build_property_rows(runtime, object->handle(), expansion, {}, warm);
    if (state.range(0) != 0) {
        expand_all_aggregate_fields(warm, expansion);
        nw::toolset::build_property_rows(runtime, object->handle(), expansion, {}, warm);
    }

    nw::toolset::PropertyTreeSnapshot snapshot;
    for (auto _ : state) {
        nw::toolset::build_property_rows(runtime, object->handle(), expansion, {}, snapshot);
        benchmark::DoNotOptimize(snapshot.rows.data());
        benchmark::ClobberMemory();
    }

    state.counters["rows"] = static_cast<double>(snapshot.rows.size());
    state.counters["snapshot_bytes"] = static_cast<double>(snapshot.data_bytes());
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(snapshot.rows.size()));
    nw::kernel::objects().destroy(object->handle());
}

template <typename Object>
void BM_smalls_property_tree_visible_slice(benchmark::State& state)
{
    auto* object = make_property_tree_object<Object>(state);
    if (!object) {
        return;
    }

    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::build_property_rows(
        nw::kernel::runtime(), object->handle(), expansion, {}, snapshot);
    constexpr uint32_t visible_row_count = 32;
    for (auto _ : state) {
        const auto rows = nw::toolset::slice_visible_property_rows(snapshot, 8, visible_row_count);
        benchmark::DoNotOptimize(rows.data());
        benchmark::DoNotOptimize(rows.size());
    }

    state.SetItemsProcessed(state.iterations() * visible_row_count);
    nw::kernel::objects().destroy(object->handle());
}

BENCHMARK_TEMPLATE(BM_smalls_property_tree_snapshot, nw::Creature)->Arg(0)->Arg(1);
BENCHMARK_TEMPLATE(BM_smalls_property_tree_snapshot, nw::Item)->Arg(0)->Arg(1);
BENCHMARK_TEMPLATE(BM_smalls_property_tree_snapshot, nw::Encounter)->Arg(0)->Arg(1);
BENCHMARK_TEMPLATE(BM_smalls_property_tree_snapshot, nw::Trigger)->Arg(0)->Arg(1);

BENCHMARK_TEMPLATE(BM_smalls_property_tree_visible_slice, nw::Creature);
BENCHMARK_TEMPLATE(BM_smalls_property_tree_visible_slice, nw::Item);
BENCHMARK_TEMPLATE(BM_smalls_property_tree_visible_slice, nw::Encounter);
BENCHMARK_TEMPLATE(BM_smalls_property_tree_visible_slice, nw::Trigger);

} // namespace
