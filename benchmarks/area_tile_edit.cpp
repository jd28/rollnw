#include "../tools/client/area_tile_brush.hpp"
#include "../tools/client/area_tile_edits.hpp"
#include "../tools/client/area_tile_interaction.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

struct AreaDimensions {
    int32_t width = 0;
    int32_t height = 0;
};

AreaDimensions area_dimensions(int64_t tile_count)
{
    switch (tile_count) {
    case 64:
        return {.width = 8, .height = 8};
    case 512:
        return {.width = 32, .height = 16};
    case 1024:
        return {.width = 32, .height = 32};
    default:
        return {};
    }
}

void configure_area(
    nw::Area& area, nw::Tileset& tileset, AreaDimensions dimensions)
{
    area.tileset = &tileset;
    area.tileset_resref = nw::Resref{"benchmark_set"};
    area.width = dimensions.width;
    area.height = dimensions.height;
    area.tiles.resize(static_cast<size_t>(dimensions.width)
        * static_cast<size_t>(dimensions.height));
}

void BM_area_tile_pick(benchmark::State& state)
{
    const auto dimensions = area_dimensions(state.range(0));
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.resize(1);
    nw::Area area;
    configure_area(area, tileset, dimensions);

    const std::array rays{
        nw::toolset::AreaTileCellRay{
            .origin = {
                static_cast<float>(dimensions.width) * 10.0f - 5.0f,
                static_cast<float>(dimensions.height) * 10.0f - 5.0f,
                100.0f,
            },
            .direction = {0.0f, 0.0f, -1.0f},
        },
    };
    std::array<nw::toolset::AreaTileCellPick, 1> output;
    for (auto _ : state) {
        nw::toolset::pick_area_tile_cells(area, rays, output);
        benchmark::DoNotOptimize(output);
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(area.tiles.size()));
}
BENCHMARK(BM_area_tile_pick)->Arg(64)->Arg(512)->Arg(1024);


void run_area_tile_apply(
    benchmark::State& state, bool with_door_hooks)
{
    const auto dimensions = area_dimensions(state.range(0));
    const auto edit_count = static_cast<uint32_t>(state.range(1));
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.resize(2);
    if (with_door_hooks) {
        const nw::TileDoorSlot slot{
            .position = {5.0f, 0.0f, 0.0f},
            .orientation = 0.0f,
            .type = 0,
        };
        tileset.tiles[0].door_slots.push_back(slot);
        tileset.tiles[1].door_slots.push_back(slot);
    }

    auto* area = nw::kernel::objects().make<nw::Area>();
    if (!area) {
        state.SkipWithError("failed to allocate benchmark area");
        return;
    }
    configure_area(*area, tileset, dimensions);
    std::vector<uint32_t> indices(edit_count);
    for (uint32_t index = 0; index < edit_count; ++index) {
        indices[index] = index;
    }
    nw::toolset::AreaTileEditBatch batch{
        .area = area->handle(),
    };
    batch.rows.reserve(indices.size());
    for (const uint32_t tile_index : indices) {
        auto after = area->tiles[tile_index];
        after.id = 1;
        after.orientation = 1;
        batch.rows.push_back({
            .tile_index = tile_index,
            .before = area->tiles[tile_index],
            .after = after,
        });
    }
    {
        auto direction = nw::toolset::ObjectEditDirection::forward;
        for (auto _ : state) {
            auto applied
                = nw::toolset::apply_area_tile_edits(batch, direction);
            benchmark::DoNotOptimize(applied);
            if (!applied.ok()) {
                state.SkipWithError(applied.diagnostic.c_str());
                break;
            }
            direction = direction == nw::toolset::ObjectEditDirection::forward
                ? nw::toolset::ObjectEditDirection::inverse
                : nw::toolset::ObjectEditDirection::forward;
        }
        state.SetItemsProcessed(
            state.iterations() * static_cast<int64_t>(edit_count));
    }

    area->clear();
    nw::kernel::objects().destroy(area->handle());
}

void BM_area_tile_apply(benchmark::State& state)
{
    run_area_tile_apply(state, false);
}
BENCHMARK(BM_area_tile_apply)
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({512, 1})
    ->Args({512, 64})
    ->Args({512, 512})
    ->Args({1024, 1})
    ->Args({1024, 64})
    ->Args({1024, 1024});

void BM_area_tile_apply_with_hooks(benchmark::State& state)
{
    run_area_tile_apply(state, true);
}
BENCHMARK(BM_area_tile_apply_with_hooks)
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({512, 1})
    ->Args({512, 512})
    ->Args({1024, 1})
    ->Args({1024, 1024});

void BM_area_tile_brush_build(benchmark::State& state)
{
    const auto dimensions = area_dimensions(state.range(0));
    const uint32_t edit_count = static_cast<uint32_t>(state.range(1));
    auto* tileset = nw::kernel::tilesets().load("ttr01");
    if (!tileset) {
        state.SkipWithError("ttr01 SET is unavailable");
        return;
    }
    auto* area = nw::kernel::objects().make<nw::Area>();
    if (!area) {
        state.SkipWithError("failed to allocate benchmark area");
        return;
    }
    configure_area(*area, *tileset, dimensions);
    area->tileset_resref = nw::Resref{"ttr01"};
    for (auto& tile : area->tiles) {
        tile = {.id = 109};
    }
    std::vector<uint32_t> indices(edit_count);
    for (uint32_t index = 0; index < edit_count; ++index) {
        indices[index] = index;
    }

    uint64_t seed = 1;
    for (auto _ : state) {
        nw::toolset::AreaTileEditBatch batch;
        auto result = nw::toolset::build_area_tile_brush_edits(
            area->handle(), indices,
            {
                .kind = nw::toolset::AreaTileBrushKind::terrain,
                .value = 1,
            },
            seed++, batch);
        benchmark::DoNotOptimize(result.status);
        benchmark::DoNotOptimize(batch.rows.data());
        benchmark::DoNotOptimize(batch.rows.size());
        if (!result.ok()) {
            state.SkipWithError(result.diagnostic.c_str());
            break;
        }
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(edit_count));
    area->clear();
    nw::kernel::objects().destroy(area->handle());
}
BENCHMARK(BM_area_tile_brush_build)
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({512, 1})
    ->Args({512, 512})
    ->Args({1024, 1})
    ->Args({1024, 1024});

void BM_area_tile_height_brush_build(benchmark::State& state)
{
    const auto dimensions = area_dimensions(state.range(0));
    const uint32_t edit_count = static_cast<uint32_t>(state.range(1));
    auto* tileset = nw::kernel::tilesets().load("ttr01");
    if (!tileset) {
        state.SkipWithError("ttr01 SET is unavailable");
        return;
    }
    auto* area = nw::kernel::objects().make<nw::Area>();
    if (!area) {
        state.SkipWithError("failed to allocate benchmark area");
        return;
    }
    configure_area(*area, *tileset, dimensions);
    area->tileset_resref = nw::Resref{"ttr01"};
    for (auto& tile : area->tiles) {
        tile = {.id = 109};
    }
    std::vector<uint32_t> corner_indices(edit_count);
    for (uint32_t index = 0; index < edit_count; ++index) {
        corner_indices[index] = index;
    }

    uint64_t seed = 1;
    for (auto _ : state) {
        nw::toolset::AreaTileEditBatch batch;
        auto result = nw::toolset::build_area_tile_height_brush_edits(
            area->handle(), corner_indices, 1, seed++, batch);
        benchmark::DoNotOptimize(result.status);
        benchmark::DoNotOptimize(batch.rows.data());
        benchmark::DoNotOptimize(batch.rows.size());
        if (!result.ok()) {
            state.SkipWithError(result.diagnostic.c_str());
            break;
        }
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(edit_count));
    area->clear();
    nw::kernel::objects().destroy(area->handle());
}
BENCHMARK(BM_area_tile_height_brush_build)
    ->Args({64, 1})
    ->Args({64, 81})
    ->Args({512, 1})
    ->Args({512, 561})
    ->Args({1024, 1})
    ->Args({1024, 1089});

} // namespace
