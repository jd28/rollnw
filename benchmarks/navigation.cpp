#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavTileBuild.hpp>
#include <nw/nav/NavWorld.hpp>

#include "../tools/client/preview_session.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

double percentile_95(const std::vector<double>& samples)
{
    if (samples.empty()) return 0.0;
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const size_t index = static_cast<size_t>(
                             std::ceil(static_cast<double>(sorted.size()) * 0.95))
        - 1;
    return sorted[index];
}

nw::nav::NavGeometry make_tile_floor(uint32_t width, uint32_t height)
{
    nw::nav::NavGeometry geometry;
    const size_t tile_count = static_cast<size_t>(width) * height;
    geometry.vertices.reserve(tile_count * 4);
    geometry.indices.reserve(tile_count * 6);
    geometry.surface.reserve(tile_count * 2);
    geometry.kind.reserve(tile_count * 2);
    geometry.owner.reserve(tile_count * 2);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tile = y * width + x;
            const uint32_t vertex = static_cast<uint32_t>(geometry.vertices.size());
            const float minimum_x = static_cast<float>(x) * 10.0f;
            const float minimum_y = static_cast<float>(y) * 10.0f;
            geometry.vertices.insert(geometry.vertices.end(), {
                                                                  {minimum_x, minimum_y, 0.0f},
                                                                  {minimum_x + 10.0f, minimum_y, 0.0f},
                                                                  {minimum_x, minimum_y + 10.0f, 0.0f},
                                                                  {minimum_x + 10.0f, minimum_y + 10.0f, 0.0f},
                                                              });
            geometry.indices.insert(geometry.indices.end(), {
                                                                vertex,
                                                                vertex + 2,
                                                                vertex + 1,
                                                                vertex + 1,
                                                                vertex + 2,
                                                                vertex + 3,
                                                            });
            geometry.surface.insert(geometry.surface.end(), {1, 1});
            geometry.kind.insert(geometry.kind.end(), {
                                                          nw::nav::NavGeometryKind::tile_wok,
                                                          nw::nav::NavGeometryKind::tile_wok,
                                                      });
            geometry.owner.insert(geometry.owner.end(), {tile, tile});
        }
    }
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavAreaBuildSource make_recast_floor(
    uint32_t width, uint32_t height)
{
    auto geometry = make_tile_floor(width, height);
    nw::nav::NavAreaBuildSource source;
    source.surface_vertices = std::move(geometry.vertices);
    source.surface_indices = std::move(geometry.indices);
    source.surface_ids = std::move(geometry.surface);
    source.surface_walkable = {0u, 1u, 0u};
    source.width = width;
    source.height = height;
    return source;
}

void append_recast_wall(nw::nav::NavAreaBuildSource& source,
    float x, float minimum_y, float maximum_y, uint32_t state)
{
    const uint32_t vertex
        = static_cast<uint32_t>(source.obstacle_vertices.size());
    source.obstacle_vertices.insert(source.obstacle_vertices.end(), {
                                                                        {x, minimum_y, 0.0f},
                                                                        {x, maximum_y, 0.0f},
                                                                        {x, minimum_y, 2.0f},
                                                                        {x, maximum_y, 2.0f},
                                                                    });
    source.obstacle_indices.insert(source.obstacle_indices.end(), {
                                                                      vertex,
                                                                      vertex + 1,
                                                                      vertex + 2,
                                                                      vertex + 2,
                                                                      vertex + 1,
                                                                      vertex + 3,
                                                                  });
    source.obstacle_surface_ids.insert(
        source.obstacle_surface_ids.end(), 2, 2u);
    source.obstacle_owner.insert(source.obstacle_owner.end(), 2, state);
    source.obstacle_state_count
        = std::max(source.obstacle_state_count, state + 1);
}

void reserve_route_outputs(size_t request_count,
    nw::Vector<glm::vec3>& corners,
    nw::nav::NavRouteArena& routes)
{
    corners.reserve(request_count * nw::nav::maximum_nav_path_corners);
    routes.polygons.reserve(
        request_count * nw::nav::maximum_nav_path_polygons);
    routes.tile_keys.reserve(
        request_count * nw::nav::maximum_nav_path_polygons);
    routes.traversals.reserve(
        request_count * nw::nav::maximum_nav_path_corners);
}

bool build_recast_world(const nw::nav::NavAreaBuildSource& source,
    std::span<const uint8_t> active,
    nw::nav::NavWorldState& world,
    nw::nav::NavTiledWorldBuildStats& stats)
{
    nw::nav::NavTileBuildConfig config;
    config.erosion_cells = 1;
    return nw::nav::build_tiled_nav_world(source, active,
               config, world, stats)
        == nw::nav::NavStatus::ok;
}

void BM_recast_cold_build(benchmark::State& state)
{
    const uint32_t edge = static_cast<uint32_t>(state.range(0));
    const auto source = make_recast_floor(edge, edge);
    nw::nav::NavTiledWorldBuildStats last_stats;
    for (auto _ : state) {
        nw::nav::NavWorldState world;
        nw::nav::NavTiledWorldBuildStats stats;
        if (!build_recast_world(source, {}, world, stats)) {
            state.SkipWithError("failed to build synthetic Recast tiled world");
            break;
        }
        last_stats = stats;
        benchmark::DoNotOptimize(world.impl.get());
    }
    state.counters["tiles"]
        = static_cast<double>(last_stats.built_tile_count);
    state.counters["polygons"]
        = static_cast<double>(last_stats.polygon_count);
    state.counters["payload_bytes"]
        = static_cast<double>(last_stats.payload_bytes);
    state.counters["polygon_graph_bytes"]
        = static_cast<double>(last_stats.polygon_graph_bytes);
    state.counters["polygon_graph_build_us"]
        = static_cast<double>(last_stats.polygon_graph_build_nanoseconds)
        / 1.0e3;
}
BENCHMARK(BM_recast_cold_build)->Arg(1)->Arg(8)->Arg(32);

void run_recast_path_batch(benchmark::State& state,
    nw::nav::NavAreaBuildSource source,
    std::span<const uint8_t> active)
{
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build_stats;
    if (!build_recast_world(source, active, world, build_stats)) {
        state.SkipWithError("failed to build path benchmark Recast world");
        return;
    }

    const size_t batch_size = static_cast<size_t>(state.range(0));
    nw::Vector<nw::nav::NavPathRequest> requests(batch_size);
    nw::Vector<nw::nav::NavPathResult> results(batch_size);
    nw::Vector<glm::vec3> corners;
    nw::nav::NavRouteArena routes;
    reserve_route_outputs(batch_size, corners, routes);
    for (size_t index = 0; index < batch_size; ++index) {
        const float offset = static_cast<float>(index % 32) * 0.125f;
        requests[index] = {
            .start = {5.0f, 155.0f + offset, 0.0f},
            .end = {315.0f, 155.0f + offset, 0.0f},
        };
    }

    for (auto _ : state) {
        const auto stats = nw::nav::find_nav_paths(
            world, requests, corners, routes, results);
        if (stats.output_count != requests.size()) {
            state.SkipWithError("Recast path batch failed");
            break;
        }
        benchmark::DoNotOptimize(corners.data());
        benchmark::DoNotOptimize(routes.polygons.data());
    }
    state.counters["polygons"]
        = static_cast<double>(build_stats.polygon_count);
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(batch_size));
}

void BM_recast_direct_path_batch(benchmark::State& state)
{
    run_recast_path_batch(state, make_recast_floor(32, 32), {});
}
BENCHMARK(BM_recast_direct_path_batch)
    ->Arg(1)
    ->Arg(32)
    ->Arg(128)
    ->ComputeStatistics("p95", percentile_95);

void BM_recast_obstructed_path_batch(benchmark::State& state)
{
    auto source = make_recast_floor(32, 32);
    append_recast_wall(source, 160.0f, -2.0f, 300.0f, 0);
    constexpr std::array<uint8_t, 1> active{1u};
    run_recast_path_batch(state, std::move(source), active);
}
BENCHMARK(BM_recast_obstructed_path_batch)
    ->Arg(1)
    ->Arg(32)
    ->Arg(128)
    ->ComputeStatistics("p95", percentile_95);

void BM_recast_door_path_batch(benchmark::State& state)
{
    auto source = make_recast_floor(32, 32);
    append_recast_wall(source, 160.0f, -2.0f, 322.0f, 0);
    source.door_links = {
        {
            .start = {159.0f, 160.0f, 0.0f},
            .end = {161.0f, 160.0f, 0.0f},
            .radius = 0.125f,
            .door_index = 0,
            .active_obstacle_state = 0,
            .side = 0,
        },
        {
            .start = {161.0f, 160.0f, 0.0f},
            .end = {159.0f, 160.0f, 0.0f},
            .radius = 0.125f,
            .door_index = 0,
            .active_obstacle_state = 0,
            .side = 1,
        },
    };
    constexpr std::array<uint8_t, 1> active{1u};
    run_recast_path_batch(state, std::move(source), active);
}
BENCHMARK(BM_recast_door_path_batch)
    ->Arg(1)
    ->Arg(32)
    ->Arg(128)
    ->ComputeStatistics("p95", percentile_95);

void BM_recast_door_rebuild(benchmark::State& state)
{
    auto source = make_recast_floor(32, 32);
    // Representative closed leaf and both directional traversal rows on a
    // tile seam. Toggling the state rebuilds the leaf and changes graph
    // topology exactly like one closed/open door transition.
    append_recast_wall(source, 160.0f, 159.0f, 161.0f, 0);
    source.door_links = {
        {
            .start = {159.0f, 160.0f, 0.0f},
            .end = {161.0f, 160.0f, 0.0f},
            .radius = 0.125f,
            .door_index = 0,
            .active_obstacle_state = 0,
            .side = 0,
        },
        {
            .start = {161.0f, 160.0f, 0.0f},
            .end = {159.0f, 160.0f, 0.0f},
            .radius = 0.125f,
            .door_index = 0,
            .active_obstacle_state = 0,
            .side = 1,
        },
    };
    constexpr std::array<uint8_t, 1> initially_active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    if (!build_recast_world(source, initially_active, world, build)) {
        state.SkipWithError("failed to build door rebuild benchmark world");
        return;
    }
    nw::Vector<uint32_t> rebuilt(
        static_cast<size_t>(source.width) * source.height);
    nw::nav::NavTileRebuildStats last_stats;
    uint8_t active = 0;
    for (auto _ : state) {
        const std::array changes{nw::nav::NavObstacleStateChange{
            .obstacle_state = 0,
            .active = active,
        }};
        nw::nav::NavTileRebuildStats stats;
        if (nw::nav::rebuild_nav_tiles(
                world, source, changes, rebuilt, stats)
            != nw::nav::NavStatus::ok) {
            state.SkipWithError("door tile rebuild failed");
            break;
        }
        last_stats = stats;
        active ^= 1u;
        benchmark::DoNotOptimize(rebuilt.data());
    }
    state.counters["affected_tiles"]
        = static_cast<double>(last_stats.affected_tile_count);
    state.counters["aggregate_tile_build_us"]
        = static_cast<double>(last_stats.tile_build_nanoseconds) / 1.0e3;
    state.counters["graph_build_us"]
        = static_cast<double>(last_stats.polygon_graph_build_nanoseconds)
        / 1.0e3;
}
BENCHMARK(BM_recast_door_rebuild)
    ->ComputeStatistics("p95", percentile_95);

void BM_nav_locomotion_batch(benchmark::State& state)
{
    constexpr uint32_t edge = 32;
    const auto source = make_recast_floor(edge, edge);
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build_stats;
    if (!build_recast_world(source, {}, world, build_stats)) {
        state.SkipWithError(
            "failed to build locomotion benchmark Recast world");
        return;
    }

    const size_t batch_size = static_cast<size_t>(state.range(0));
    nw::Vector<nw::nav::NavAgentRegistrationInput> registrations(batch_size);
    for (size_t index = 0; index < batch_size; ++index) {
        const float offset = static_cast<float>(index % 64) * 0.01f;
        registrations[index].position = {5.0f + offset, 5.0f + offset, 0.0f};
    }
    nw::Vector<nw::nav::NavAgentRegistrationResult> registered(batch_size);
    if (nw::nav::register_nav_agents(world, registrations, registered).output_count
        != batch_size) {
        state.SkipWithError("failed to register locomotion benchmark agents");
        return;
    }

    nw::Vector<nw::nav::NavAgentMotionInput> inputs(batch_size);
    nw::Vector<nw::nav::NavAgentMotionResult> results(batch_size);
    for (size_t index = 0; index < batch_size; ++index) {
        inputs[index] = {
            registered[index].agent,
            registered[index].position,
            {0.01f, 0.0f, 0.0f},
        };
    }

    for (auto _ : state) {
        const auto stats = nw::nav::move_nav_agents(world, inputs, results);
        if (stats.output_count != inputs.size()) {
            state.SkipWithError("locomotion batch failed");
            break;
        }
        benchmark::DoNotOptimize(results.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(batch_size));
}
BENCHMARK(BM_nav_locomotion_batch)->Arg(1)->Arg(32)->Arg(128);

void BM_preview_fixed_tick(benchmark::State& state)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module || module->areas.empty()) {
        state.SkipWithError("failed to load preview benchmark module");
        return;
    }
    auto* area = module->get_area(0);
    if (!area) {
        nw::kernel::unload_module();
        state.SkipWithError("preview benchmark module has no area");
        return;
    }

    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    const auto started = nw::toolset::start_toolset_preview(session, start);
    if (!started.ok()) {
        nw::kernel::unload_module();
        state.SkipWithError(started.diagnostic.c_str());
        return;
    }

    const size_t sample_count = static_cast<size_t>(state.range(0));
    std::array<nw::toolset::PreviewInputSample, 6> samples{};
    std::array<nw::ObjectSpatialState, 1> output{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
    for (auto _ : state) {
        const auto stats = nw::toolset::tick_toolset_preview(
            session, std::span{samples}.first(sample_count), output, locomotion);
        if (stats.output_count != output.size()) {
            state.SkipWithError("preview tick failed");
            break;
        }
        benchmark::DoNotOptimize(output.data());
    }
    state.counters["nav_payload_bytes"] = static_cast<double>(started.stats.navigation_payload_bytes);
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(sample_count));

    nw::toolset::stop_toolset_preview(session);
    nw::kernel::unload_module();
}
BENCHMARK(BM_preview_fixed_tick)->Arg(1)->Arg(6);

void BM_preview_cold_start(benchmark::State& state)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    if (!module || module->areas.empty()) {
        state.SkipWithError("failed to load preview cold-start benchmark module");
        return;
    }
    auto* area = module->get_area(0);
    if (!area) {
        nw::kernel::unload_module();
        state.SkipWithError("preview cold-start benchmark module has no area");
        return;
    }

    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    nw::toolset::PreviewSessionStartStats last_stats;
    for (auto _ : state) {
        nw::toolset::ToolsetPreviewSession session;
        const auto started = nw::toolset::start_toolset_preview(session, start);
        if (!started.ok()) {
            state.SkipWithError(started.diagnostic.c_str());
            break;
        }
        last_stats = started.stats;
        benchmark::DoNotOptimize(session.actor());
        nw::toolset::stop_toolset_preview(session);
    }
    state.counters["tiles"] = static_cast<double>(last_stats.tile_count);
    state.counters["surface_triangles"]
        = static_cast<double>(last_stats.authored_surface_triangle_count);
    state.counters["obstacle_triangles"]
        = static_cast<double>(last_stats.obstacle_triangle_count);
    state.counters["nav_payload_bytes"] = static_cast<double>(last_stats.navigation_payload_bytes);

    nw::kernel::unload_module();
}
BENCHMARK(BM_preview_cold_start);

} // namespace
