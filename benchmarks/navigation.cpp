#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavWorld.hpp>

#include "../tools/client/preview_session.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace {

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

bool build_floor_world(
    uint32_t width,
    uint32_t height,
    const nw::nav::NavGeometry& geometry,
    nw::nav::NavWorldState& world,
    nw::nav::NavBuildStats& stats)
{
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    return nw::nav::build_nav_world(geometry, walkable, width, height, world, stats)
        == nw::nav::NavStatus::ok;
}

void BM_nav_cold_build(benchmark::State& state)
{
    const uint32_t edge = static_cast<uint32_t>(state.range(0));
    const auto geometry = make_tile_floor(edge, edge);
    nw::nav::NavBuildStats last_stats;
    for (auto _ : state) {
        nw::nav::NavWorldState world;
        nw::nav::NavBuildStats stats;
        if (!build_floor_world(edge, edge, geometry, world, stats)) {
            state.SkipWithError("failed to build synthetic NWN tile navigation world");
            break;
        }
        last_stats = stats;
        benchmark::DoNotOptimize(world.impl.get());
    }
    state.counters["tiles"] = static_cast<double>(last_stats.tile_count);
    state.counters["triangles"] = static_cast<double>(last_stats.polygon_count);
    state.counters["payload_bytes"] = static_cast<double>(last_stats.payload_bytes);
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(geometry.triangle_count()));
}
BENCHMARK(BM_nav_cold_build)->Arg(1)->Arg(8)->Arg(32);

void BM_nav_path_batch(benchmark::State& state)
{
    constexpr uint32_t edge = 32;
    const auto geometry = make_tile_floor(edge, edge);
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    if (!build_floor_world(edge, edge, geometry, world, build_stats)) {
        state.SkipWithError("failed to build path benchmark navigation world");
        return;
    }

    const size_t batch_size = static_cast<size_t>(state.range(0));
    nw::Vector<nw::nav::NavPathRequest> requests(batch_size);
    nw::Vector<nw::nav::NavPathResult> results(batch_size);
    nw::Vector<glm::vec3> corners;
    corners.reserve(batch_size * 64);
    for (size_t index = 0; index < batch_size; ++index) {
        const float inset = 1.0f + static_cast<float>(index % 8) * 0.5f;
        requests[index] = {
            {inset, inset, 0.0f},
            {static_cast<float>(edge) * 10.0f - inset,
                static_cast<float>(edge) * 10.0f - inset, 0.0f},
            0.3f,
        };
    }

    for (auto _ : state) {
        const auto stats = nw::nav::find_nav_paths(world, requests, corners, results);
        if (stats.output_count != requests.size()) {
            state.SkipWithError("path batch failed");
            break;
        }
        benchmark::DoNotOptimize(corners.data());
    }
    state.counters["tiles"] = static_cast<double>(build_stats.tile_count);
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(batch_size));
}
BENCHMARK(BM_nav_path_batch)->Arg(1)->Arg(32)->Arg(128);

void BM_nav_locomotion_batch(benchmark::State& state)
{
    constexpr uint32_t edge = 32;
    const auto geometry = make_tile_floor(edge, edge);
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    if (!build_floor_world(edge, edge, geometry, world, build_stats)) {
        state.SkipWithError("failed to build locomotion benchmark navigation world");
        return;
    }

    const size_t batch_size = static_cast<size_t>(state.range(0));
    nw::Vector<nw::nav::NavAgentRegistrationInput> registrations(batch_size);
    for (size_t index = 0; index < batch_size; ++index) {
        const float offset = static_cast<float>(index % 64) * 0.01f;
        registrations[index].position = {5.0f + offset, 5.0f + offset, 0.0f};
        registrations[index].clearance = 0.3f;
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
    state.counters["triangles"] = static_cast<double>(last_stats.navigation_triangle_count);
    state.counters["blocker_triangles"] = static_cast<double>(last_stats.blocker_triangle_count);
    state.counters["nav_payload_bytes"] = static_cast<double>(last_stats.navigation_payload_bytes);

    nw::kernel::unload_module();
}
BENCHMARK(BM_preview_cold_start);

} // namespace
