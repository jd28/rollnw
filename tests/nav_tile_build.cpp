#include <gtest/gtest.h>

#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavTileBuild.hpp>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace {

nw::nav::NavGeometry make_two_tile_recast_floor()
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {20.0f, 0.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
        {20.0f, 10.0f, 0.0f},
    };
    geometry.indices = {
        0,
        2,
        1,
        1,
        2,
        3,
        4,
        6,
        5,
        5,
        6,
        7,
    };
    geometry.surface.assign(4, 1u);
    geometry.kind.assign(4, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner = {0u, 0u, 1u, 1u};
    return geometry;
}

nw::nav::NavAreaBuildSource make_two_tile_area_source()
{
    auto geometry = make_two_tile_recast_floor();
    nw::nav::NavAreaBuildSource source;
    source.surface_vertices = std::move(geometry.vertices);
    source.surface_indices = std::move(geometry.indices);
    source.surface_ids = std::move(geometry.surface);
    source.surface_walkable = {0u, 1u};
    source.width = 2;
    source.height = 1;
    return source;
}

void append_wall(nw::nav::NavAreaBuildSource& source, float x,
    uint32_t obstacle_state)
{
    if (source.obstacle_vertices.size() > UINT32_MAX - 4) return;
    const uint32_t vertex = static_cast<uint32_t>(source.obstacle_vertices.size());
    source.obstacle_vertices.insert(source.obstacle_vertices.end(), {
                                                                        {x, -2.0f, 0.0f},
                                                                        {x, 12.0f, 0.0f},
                                                                        {x, -2.0f, 2.0f},
                                                                        {x, 12.0f, 2.0f},
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
    source.obstacle_owner.insert(
        source.obstacle_owner.end(), 2, obstacle_state);
    source.obstacle_state_count
        = std::max(source.obstacle_state_count, obstacle_state + 1);
}

nw::nav::NavAreaBuildSource make_single_tile_area_source()
{
    nw::nav::NavAreaBuildSource source;
    source.surface_vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
    };
    source.surface_indices = {0, 2, 1, 1, 2, 3};
    source.surface_ids = {1u, 1u};
    source.surface_walkable = {0u, 1u, 0u};
    source.width = 1;
    source.height = 1;
    return source;
}

nw::nav::NavAreaBuildSource make_four_tile_area_source()
{
    nw::nav::NavAreaBuildSource source;
    source.surface_walkable = {0u, 1u, 0u};
    source.width = 2;
    source.height = 2;
    for (uint32_t y = 0; y < source.height; ++y) {
        for (uint32_t x = 0; x < source.width; ++x) {
            const uint32_t vertex
                = static_cast<uint32_t>(source.surface_vertices.size());
            const float minimum_x = static_cast<float>(x) * 10.0f;
            const float minimum_y = static_cast<float>(y) * 10.0f;
            source.surface_vertices.insert(source.surface_vertices.end(), {
                                                                              {minimum_x, minimum_y, 0.0f},
                                                                              {minimum_x + 10.0f, minimum_y, 0.0f},
                                                                              {minimum_x, minimum_y + 10.0f, 0.0f},
                                                                              {minimum_x + 10.0f, minimum_y + 10.0f, 0.0f},
                                                                          });
            source.surface_indices.insert(source.surface_indices.end(), {
                                                                            vertex,
                                                                            vertex + 2,
                                                                            vertex + 1,
                                                                            vertex + 1,
                                                                            vertex + 2,
                                                                            vertex + 3,
                                                                        });
            source.surface_ids.insert(source.surface_ids.end(), {1u, 1u});
        }
    }
    return source;
}

struct PathProbe {
    nw::nav::NavStatus status = nw::nav::NavStatus::rejected;
    glm::vec3 endpoint{0.0f};
};

PathProbe probe_path(const nw::nav::NavWorldState& world,
    glm::vec3 start = {2.0f, 5.0f, 0.0f},
    glm::vec3 end = {8.0f, 5.0f, 0.0f})
{
    const std::array request{nw::nav::NavPathRequest{start, end}};
    std::array<nw::nav::NavPathResult, 1> result{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, request, corners, result);
    PathProbe probe{.status = result[0].status};
    if (result[0].corner_count > 0) {
        probe.endpoint = corners[result[0].corner_offset
            + result[0].corner_count - 1];
    }
    return probe;
}

} // namespace

TEST(NavTileBuild, BuildsTriangleRangesAcrossTileBorder)
{
    const auto geometry = make_two_tile_recast_floor();
    nw::nav::NavTileTriangleRanges ranges;
    const auto stats = nw::nav::build_nav_tile_triangle_ranges(
        geometry.vertices, geometry.indices, 2, 1, 0.5f, ranges);

    ASSERT_EQ(stats.status, nw::nav::NavStatus::ok);
    EXPECT_EQ(stats.input_triangle_count, 4u);
    EXPECT_EQ(stats.tile_count, 2u);
    EXPECT_EQ(ranges.offsets.size(), 3u);
    EXPECT_EQ(ranges.tile({0, 0}, 2).size(), 4u);
    EXPECT_EQ(ranges.tile({1, 0}, 2).size(), 4u);
}

TEST(NavTileBuild, ConnectsAssembledAreaAcrossTileSeam)
{
    const auto geometry = make_two_tile_recast_floor();
    nw::nav::NavTileBuildConfig config;
    config.cell_size = 0.25f;
    config.cell_height = 0.1f;
    config.erosion_cells = 1;
    const float border
        = static_cast<float>(config.erosion_cells + 3) * config.cell_size;
    nw::nav::NavTileTriangleRanges ranges;
    ASSERT_EQ(nw::nav::build_nav_tile_triangle_ranges(
                  geometry.vertices, geometry.indices, 2, 1, border, ranges)
                  .status,
        nw::nav::NavStatus::ok);

    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{
        dtAllocNavMesh(), dtFreeNavMesh};
    ASSERT_NE(mesh, nullptr);
    dtNavMeshParams params{};
    params.tileWidth = nw::nav::nav_tile_size;
    params.tileHeight = nw::nav::nav_tile_size;
    params.maxTiles = 2;
    params.maxPolys = 64;
    ASSERT_TRUE(dtStatusSucceed(mesh->init(&params)));

    const std::array<uint8_t, 2> walkable{0u, 1u};
    for (uint32_t tile_x = 0; tile_x < 2; ++tile_x) {
        nw::nav::NavTileData tile_data;
        const nw::nav::NavTileBuildInput input{
            .surface_vertices = geometry.vertices,
            .surface_indices = geometry.indices,
            .surface_ids = geometry.surface,
            .surface_triangles = ranges.tile({tile_x, 0}, 2),
            .surface_walkable = walkable,
            .tile = {tile_x, 0},
        };
        const auto build = nw::nav::build_nav_tile_data(
            input, config, tile_data);
        ASSERT_EQ(build.status, nw::nav::NavStatus::ok);
        ASSERT_FALSE(tile_data.empty());
        const int payload_size = tile_data.size();
        unsigned char* payload = tile_data.release();
        if (dtStatusFailed(mesh->addTile(payload, payload_size,
                DT_TILE_FREE_DATA, 0, nullptr))) {
            dtFree(payload);
            FAIL() << "failed to add Recast tile " << tile_x;
        }
    }

    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
        dtAllocNavMeshQuery(), dtFreeNavMeshQuery};
    ASSERT_NE(query, nullptr);
    ASSERT_TRUE(dtStatusSucceed(query->init(mesh.get(), 256)));
    dtQueryFilter filter;
    filter.setIncludeFlags(nw::nav::nav_walkable_flag);
    const std::array<float, 3> extents{1.0f, 2.0f, 1.0f};
    const std::array<float, 3> start{1.0f, 0.0f, 5.0f};
    const std::array<float, 3> end{19.0f, 0.0f, 5.0f};
    std::array<float, 3> nearest_start{};
    std::array<float, 3> nearest_end{};
    dtPolyRef start_polygon = 0;
    dtPolyRef end_polygon = 0;
    ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(start.data(),
        extents.data(), &filter, &start_polygon, nearest_start.data())));
    ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(end.data(),
        extents.data(), &filter, &end_polygon, nearest_end.data())));
    ASSERT_NE(start_polygon, 0u);
    ASSERT_NE(end_polygon, 0u);

    std::array<dtPolyRef, 32> corridor{};
    int corridor_count = 0;
    ASSERT_TRUE(dtStatusSucceed(query->findPath(start_polygon, end_polygon,
        nearest_start.data(), nearest_end.data(), &filter, corridor.data(),
        &corridor_count, static_cast<int>(corridor.size()))));
    ASSERT_GT(corridor_count, 0);
    EXPECT_EQ(corridor[static_cast<size_t>(corridor_count - 1)], end_polygon);
}

TEST(NavTileBuild, BuildsCompleteTiledWorldSnapshot)
{
    const auto source = make_two_tile_area_source();
    nw::nav::NavTileBuildConfig config;
    config.erosion_cells = 1;
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;

    ASSERT_EQ(nw::nav::build_tiled_nav_world(
                  source, {}, config, world, build),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build.status, nw::nav::NavStatus::ok);
    EXPECT_EQ(build.declared_tile_count, 2u);
    EXPECT_EQ(build.built_tile_count, 2u);
    EXPECT_EQ(build.empty_tile_count, 0u);
    EXPECT_EQ(build.rejected_tile_count, 0u);
    EXPECT_GT(build.polygon_count, 0u);
    EXPECT_GT(build.payload_bytes, 0u);

    constexpr std::array requests{nw::nav::NavPathRequest{
        {1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.0f}}};
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    const auto path_stats
        = nw::nav::find_nav_paths(world, requests, corners, results);
    ASSERT_EQ(path_stats.output_count, 1u);
    ASSERT_EQ(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_GT(results[0].corner_count, 0u);
    const auto& endpoint = corners[results[0].corner_offset
        + results[0].corner_count - 1];
    EXPECT_NEAR(endpoint.x, 19.0f, config.cell_size);
    EXPECT_NEAR(endpoint.y, 5.0f, config.cell_size);
}

TEST(NavTileBuild, TiledWorldRasterizesActiveObstacleState)
{
    nw::nav::NavAreaBuildSource source;
    source.surface_vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
    };
    source.surface_indices = {0, 2, 1, 1, 2, 3};
    source.surface_ids = {1u, 1u};
    source.surface_walkable = {0u, 1u, 0u};
    source.obstacle_vertices = {
        {5.0f, -2.0f, 0.0f},
        {5.0f, 12.0f, 0.0f},
        {5.0f, -2.0f, 2.0f},
        {5.0f, 12.0f, 2.0f},
    };
    source.obstacle_indices = {0, 1, 2, 2, 1, 3};
    source.obstacle_surface_ids = {2u, 2u};
    source.obstacle_owner = {0u, 0u};
    source.obstacle_state_count = 1;
    source.width = 1;
    source.height = 1;
    nw::nav::NavTileBuildConfig config;
    config.erosion_cells = 1;
    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;

    ASSERT_EQ(nw::nav::build_tiled_nav_world(
                  source, active, config, world, build),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build.rasterized_obstacle_triangle_count, 2u);

    constexpr std::array requests{nw::nav::NavPathRequest{
        {2.0f, 5.0f, 0.0f}, {8.0f, 5.0f, 0.0f}}};
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    ASSERT_EQ(nw::nav::find_nav_paths(world, requests, corners, results)
                  .output_count,
        1u);
    ASSERT_NE(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_GT(results[0].corner_count, 0u);
    const auto& endpoint = corners[results[0].corner_offset
        + results[0].corner_count - 1];
    EXPECT_LT(endpoint.x, 5.0f);
}

TEST(NavTileBuild, TiledWorldRejectsObstacleStateShape)
{
    auto source = make_two_tile_area_source();
    source.obstacle_state_count = 1;
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats stats;

    EXPECT_EQ(nw::nav::build_tiled_nav_world(source, {},
                  nw::nav::NavTileBuildConfig{}, world, stats),
        nw::nav::NavStatus::rejected);
    EXPECT_EQ(stats.status, nw::nav::NavStatus::rejected);
}

TEST(NavTileBuild, RebuildsAffectedObstacleTile)
{
    auto source = make_single_tile_area_source();
    append_wall(source, 5.0f, 0);
    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);
    EXPECT_NE(probe_path(world).status, nw::nav::NavStatus::ok);

    constexpr std::array changes{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
    };
    std::array<uint32_t, 1> rebuilt{};
    nw::nav::NavTileRebuildStats stats;
    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, rebuilt, stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(stats.changed_state_count, 1u);
    EXPECT_EQ(stats.affected_tile_count, 1u);
    EXPECT_EQ(stats.rebuilt_tile_count, 1u);
    EXPECT_EQ(rebuilt[0], 0u);

    const auto path = probe_path(world);
    EXPECT_EQ(path.status, nw::nav::NavStatus::ok);
    EXPECT_NEAR(path.endpoint.x, 8.0f, 0.125f);
}

TEST(NavTileBuild, RebuildBatchDeduplicatesStatesAndTiles)
{
    auto source = make_single_tile_area_source();
    append_wall(source, 4.0f, 0);
    append_wall(source, 6.0f, 1);
    constexpr std::array<uint8_t, 2> active{1u, 1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);

    constexpr std::array changes{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
        nw::nav::NavObstacleStateChange{.obstacle_state = 1, .active = 0},
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
    };
    std::array<uint32_t, 1> rebuilt{};
    nw::nav::NavTileRebuildStats stats;
    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, rebuilt, stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(stats.input_count, 3u);
    EXPECT_EQ(stats.changed_state_count, 2u);
    EXPECT_EQ(stats.duplicate_count, 1u);
    EXPECT_EQ(stats.affected_tile_count, 1u);
    EXPECT_EQ(stats.rebuilt_tile_count, 1u);
    EXPECT_EQ(probe_path(world).status, nw::nav::NavStatus::ok);

    constexpr std::array contradictory{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 1},
    };
    EXPECT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, contradictory, rebuilt, stats),
        nw::nav::NavStatus::rejected);
    EXPECT_EQ(probe_path(world).status, nw::nav::NavStatus::ok);
}

TEST(NavTileBuild, RebuildsFourAffectedTilesAsOneBatch)
{
    auto source = make_four_tile_area_source();
    const uint32_t vertex
        = static_cast<uint32_t>(source.obstacle_vertices.size());
    source.obstacle_vertices = {
        {10.0f, 9.0f, 0.0f},
        {10.0f, 11.0f, 0.0f},
        {10.0f, 9.0f, 2.0f},
        {10.0f, 11.0f, 2.0f},
    };
    source.obstacle_indices = {
        vertex,
        vertex + 1,
        vertex + 2,
        vertex + 2,
        vertex + 1,
        vertex + 3,
    };
    source.obstacle_surface_ids = {2u, 2u};
    source.obstacle_owner = {0u, 0u};
    source.obstacle_state_count = 1;

    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);

    constexpr std::array changes{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
    };
    std::array<uint32_t, 4> rebuilt{};
    nw::nav::NavTileRebuildStats stats;
    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, rebuilt, stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(stats.affected_tile_count, 4u);
    EXPECT_EQ(stats.rebuilt_tile_count, 4u);
    EXPECT_EQ(rebuilt, (std::array<uint32_t, 4>{0u, 1u, 2u, 3u}));
}

TEST(NavTileBuild, RebuildOutputFullLeavesOldTileInstalled)
{
    auto source = make_single_tile_area_source();
    append_wall(source, 5.0f, 0);
    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);
    constexpr std::array changes{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
    };
    nw::nav::NavTileRebuildStats stats;

    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, {}, stats),
        nw::nav::NavStatus::output_full);
    EXPECT_EQ(stats.rebuilt_tile_count, 0u);
    EXPECT_NE(probe_path(world).status, nw::nav::NavStatus::ok);

    std::array<uint32_t, 1> rebuilt{};
    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, rebuilt, stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(probe_path(world).status, nw::nav::NavStatus::ok);
}

TEST(NavTileBuild, ClosedDoorPlansThroughTaggedTraversalLink)
{
    auto source = make_single_tile_area_source();
    append_wall(source, 5.0f, 0);
    source.door_links = {
        {
            .start = {4.0f, 5.0f, 0.0f},
            .end = {6.0f, 5.0f, 0.0f},
            .radius = 0.5f,
            .door_index = 7,
            .active_obstacle_state = 0,
            .side = 0,
        },
        {
            .start = {6.0f, 5.0f, 0.0f},
            .end = {4.0f, 5.0f, 0.0f},
            .radius = 0.5f,
            .door_index = 7,
            .active_obstacle_state = 0,
            .side = 1,
        },
    };
    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);
    ASSERT_EQ(build.enabled_door_link_count, 2u);

    constexpr std::array requests{nw::nav::NavPathRequest{
        {2.0f, 5.0f, 0.0f}, {8.0f, 5.0f, 0.0f}}};
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::NavRouteArena routes;
    corners.reserve(nw::nav::maximum_nav_path_corners);
    routes.polygons.reserve(nw::nav::maximum_nav_path_polygons);
    routes.tile_keys.reserve(nw::nav::maximum_nav_path_polygons);
    routes.traversals.reserve(nw::nav::maximum_nav_path_corners);
    ASSERT_EQ(nw::nav::find_nav_paths(
                  world, requests, corners, routes, results)
                  .output_count,
        1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_GT(results[0].polygon_count, 0u);
    ASSERT_EQ(results[0].tile_count, 1u);
    EXPECT_EQ(routes.tile_keys[results[0].tile_offset], 0u);
    ASSERT_EQ(results[0].traversal_count, 1u);
    const auto& traversal
        = routes.traversals[results[0].traversal_offset];
    EXPECT_EQ(traversal.door_index, 7u);
    EXPECT_EQ(traversal.side, 0u);
    ASSERT_LT(traversal.corner, results[0].corner_count);
    EXPECT_LT(corners[results[0].corner_offset + traversal.corner].x, 5.0f);

    const glm::vec3 approach
        = corners[results[0].corner_offset + traversal.corner];
    constexpr std::array registrations{nw::nav::NavAgentRegistrationInput{
        .position = {2.0f, 5.0f, 0.0f},
    }};
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(
                  world, registrations, registered)
                  .output_count,
        1u);
    const std::array approach_motion{nw::nav::NavAgentMotionInput{
        .agent = registered[0].agent,
        .position = registered[0].position,
        .desired_displacement = approach - registered[0].position,
    }};
    std::array<nw::nav::NavAgentMotionResult, 1> approached{};
    ASSERT_EQ(nw::nav::move_nav_agents(
                  world, approach_motion, approached)
                  .output_count,
        1u);
    EXPECT_LT(approached[0].position.x, 5.0f);

    constexpr std::array changes{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
    };
    std::array<uint32_t, 1> rebuilt{};
    nw::nav::NavTileRebuildStats rebuild;
    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, rebuilt, rebuild),
        nw::nav::NavStatus::ok);
    ASSERT_EQ(nw::nav::find_nav_paths(
                  world, requests, corners, routes, results)
                  .output_count,
        1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    EXPECT_EQ(results[0].traversal_count, 0u);

    const std::array continue_request{nw::nav::NavPathRequest{
        approached[0].position, {8.0f, 5.0f, 0.0f}}};
    ASSERT_EQ(nw::nav::find_nav_paths(
                  world, continue_request, corners, routes, results)
                  .output_count,
        1u);
    ASSERT_EQ(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_EQ(results[0].traversal_count, 0u);
    const std::array continue_motion{nw::nav::NavAgentMotionInput{
        .agent = registered[0].agent,
        .position = approached[0].position,
        .desired_displacement = glm::vec3{8.0f, 5.0f, 0.0f}
            - approached[0].position,
    }};
    std::array<nw::nav::NavAgentMotionResult, 1> continued{};
    ASSERT_EQ(nw::nav::move_nav_agents(
                  world, continue_motion, continued)
                  .output_count,
        1u);
    EXPECT_GT(continued[0].position.x, 5.0f);
    EXPECT_NEAR(continued[0].position.x, 8.0f, 0.125f);
}

TEST(NavTileBuild, BatchedCorridorReusePreservesRequestEndpoints)
{
    auto source = make_single_tile_area_source();
    append_wall(source, 5.0f, 0);
    source.door_links = {
        {
            .start = {4.0f, 5.0f, 0.0f},
            .end = {6.0f, 5.0f, 0.0f},
            .radius = 0.5f,
            .door_index = 7,
            .active_obstacle_state = 0,
            .side = 0,
        },
        {
            .start = {6.0f, 5.0f, 0.0f},
            .end = {4.0f, 5.0f, 0.0f},
            .radius = 0.5f,
            .door_index = 7,
            .active_obstacle_state = 0,
            .side = 1,
        },
    };
    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{2.0f, 4.5f, 0.0f}, {8.0f, 4.5f, 0.0f}},
        nw::nav::NavPathRequest{{2.0f, 5.5f, 0.0f}, {8.0f, 5.5f, 0.0f}},
    };
    std::array<nw::nav::NavPathResult, requests.size()> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::NavRouteArena routes;
    corners.reserve(requests.size() * nw::nav::maximum_nav_path_corners);
    routes.polygons.reserve(
        requests.size() * nw::nav::maximum_nav_path_polygons);
    routes.tile_keys.reserve(
        requests.size() * nw::nav::maximum_nav_path_polygons);
    routes.traversals.reserve(
        requests.size() * nw::nav::maximum_nav_path_corners);

    const auto stats = nw::nav::find_nav_paths(
        world, requests, corners, routes, results);

    ASSERT_EQ(stats.output_count, requests.size());
    for (size_t index = 0; index < requests.size(); ++index) {
        ASSERT_EQ(results[index].status, nw::nav::NavStatus::ok);
        ASSERT_GT(results[index].corner_count, 1u);
        ASSERT_EQ(results[index].traversal_count, 1u);
        const auto first = corners[results[index].corner_offset];
        const auto last = corners[results[index].corner_offset
            + results[index].corner_count - 1];
        EXPECT_NEAR(first.x, requests[index].start.x, 0.125f);
        EXPECT_NEAR(first.y, requests[index].start.y, 0.125f);
        EXPECT_NEAR(last.x, requests[index].end.x, 0.125f);
        EXPECT_NEAR(last.y, requests[index].end.y, 0.125f);
    }
    EXPECT_NE(corners[results[0].corner_offset].y,
        corners[results[1].corner_offset].y);
}

TEST(NavTileBuild, RouteAwarePathDoesNotGrowCallerArenas)
{
    auto source = make_single_tile_area_source();
    constexpr std::array<uint8_t, 0> active{};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, active,
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);

    constexpr std::array requests{nw::nav::NavPathRequest{
        {1.0f, 5.0f, 0.0f}, {9.0f, 5.0f, 0.0f}}};
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::NavRouteArena routes;

    const auto stats = nw::nav::find_nav_paths(
        world, requests, corners, routes, results);

    EXPECT_EQ(stats.output_count, 0u);
    EXPECT_EQ(stats.rejected_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::output_full);
    EXPECT_EQ(corners.capacity(), 0u);
    EXPECT_EQ(routes.polygons.capacity(), 0u);
    EXPECT_EQ(routes.tile_keys.capacity(), 0u);
    EXPECT_EQ(routes.traversals.capacity(), 0u);
}

TEST(NavTileBuild, RouteInvalidationIgnoresTraversedTiles)
{
    const auto source = make_two_tile_area_source();
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(nw::nav::build_tiled_nav_world(source, {},
                  nw::nav::NavTileBuildConfig{}, world, build),
        nw::nav::NavStatus::ok);

    constexpr std::array requests{nw::nav::NavPathRequest{
        {1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.0f}}};
    std::array<nw::nav::NavPathResult, 1> path{};
    nw::Vector<glm::vec3> corners;
    nw::nav::NavRouteArena arena;
    corners.reserve(nw::nav::maximum_nav_path_corners);
    arena.polygons.reserve(nw::nav::maximum_nav_path_polygons);
    arena.tile_keys.reserve(nw::nav::maximum_nav_path_polygons);
    arena.traversals.reserve(nw::nav::maximum_nav_path_corners);
    ASSERT_EQ(nw::nav::find_nav_paths(
                  world, requests, corners, arena, path)
                  .output_count,
        1u);
    ASSERT_EQ(path[0].corner_count, 2u);
    ASSERT_EQ(path[0].tile_count, 2u);

    uint32_t first_tile_one = path[0].polygon_count;
    for (uint32_t polygon = 0; polygon < path[0].polygon_count; ++polygon) {
        if (arena.polygons[path[0].polygon_offset + polygon].tile_key == 1u) {
            first_tile_one = polygon;
            break;
        }
    }
    ASSERT_LT(first_tile_one, path[0].polygon_count);
    const std::array active{nw::nav::NavRouteInvalidationInput{
        .route = path[0],
        .first_remaining_polygon = first_tile_one,
    }};
    std::array<nw::nav::NavRouteInvalidationResult, 1> invalidated{};

    constexpr std::array rebuilt_past{0u};
    auto stats = nw::nav::invalidate_nav_routes(
        arena, active, rebuilt_past, invalidated);
    ASSERT_EQ(stats.output_count, 1u);
    EXPECT_FALSE(invalidated[0].invalidated);

    constexpr std::array rebuilt_remaining{1u};
    stats = nw::nav::invalidate_nav_routes(
        arena, active, rebuilt_remaining, invalidated);
    ASSERT_EQ(stats.output_count, 1u);
    EXPECT_TRUE(invalidated[0].invalidated);

    constexpr std::array unordered_changes{1u, 0u};
    stats = nw::nav::invalidate_nav_routes(
        arena, active, unordered_changes, invalidated);
    EXPECT_EQ(stats.rejected_count, 1u);
    EXPECT_EQ(invalidated[0].status, nw::nav::NavStatus::rejected);
}

TEST(NavTileBuild, ActiveObstacleSeparatesWalkableFloor)
{
    nw::nav::NavGeometry floor;
    floor.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
    };
    floor.indices = {0, 2, 1, 1, 2, 3};
    floor.surface.assign(2, 1u);

    nw::nav::NavGeometry obstacle;
    obstacle.vertices = {
        {5.0f, -2.0f, 0.0f},
        {5.0f, 12.0f, 0.0f},
        {5.0f, -2.0f, 2.0f},
        {5.0f, 12.0f, 2.0f},
    };
    obstacle.indices = {0, 1, 2, 2, 1, 3};
    obstacle.surface.assign(2, 2u);

    nw::nav::NavTileBuildConfig config;
    config.cell_size = 0.25f;
    config.cell_height = 0.1f;
    config.erosion_cells = 1;
    const std::array<uint32_t, 2> floor_triangles{0u, 1u};
    const std::array<uint32_t, 2> obstacle_triangles{0u, 1u};
    const std::array<uint32_t, 2> obstacle_owners{0u, 0u};
    const std::array<uint8_t, 3> walkable{0u, 1u, 0u};
    const std::array<uint8_t, 1> active{1u};
    nw::nav::NavTileData tile_data;
    const nw::nav::NavTileBuildInput input{
        .surface_vertices = floor.vertices,
        .surface_indices = floor.indices,
        .surface_ids = floor.surface,
        .surface_triangles = floor_triangles,
        .surface_walkable = walkable,
        .obstacle_vertices = obstacle.vertices,
        .obstacle_indices = obstacle.indices,
        .obstacle_surface_ids = obstacle.surface,
        .obstacle_owner = obstacle_owners,
        .obstacle_triangles = obstacle_triangles,
        .obstacle_active = active,
        .tile = {0, 0},
    };
    const auto build
        = nw::nav::build_nav_tile_data(input, config, tile_data);
    ASSERT_EQ(build.status, nw::nav::NavStatus::ok);
    ASSERT_EQ(build.rasterized_obstacle_triangle_count, 2u);
    ASSERT_FALSE(tile_data.empty());

    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{
        dtAllocNavMesh(), dtFreeNavMesh};
    ASSERT_NE(mesh, nullptr);
    dtNavMeshParams params{};
    params.tileWidth = nw::nav::nav_tile_size;
    params.tileHeight = nw::nav::nav_tile_size;
    params.maxTiles = 1;
    params.maxPolys = 64;
    ASSERT_TRUE(dtStatusSucceed(mesh->init(&params)));
    const int payload_size = tile_data.size();
    unsigned char* payload = tile_data.release();
    if (dtStatusFailed(mesh->addTile(
            payload, payload_size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(payload);
        FAIL() << "failed to add obstacle test tile";
    }

    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
        dtAllocNavMeshQuery(), dtFreeNavMeshQuery};
    ASSERT_NE(query, nullptr);
    ASSERT_TRUE(dtStatusSucceed(query->init(mesh.get(), 64)));
    dtQueryFilter filter;
    filter.setIncludeFlags(nw::nav::nav_walkable_flag);
    const std::array<float, 3> extents{1.0f, 2.0f, 1.0f};
    const std::array<float, 3> start{2.0f, 0.0f, 5.0f};
    const std::array<float, 3> end{8.0f, 0.0f, 5.0f};
    std::array<float, 3> nearest_start{};
    std::array<float, 3> nearest_end{};
    dtPolyRef start_polygon = 0;
    dtPolyRef end_polygon = 0;
    ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(start.data(),
        extents.data(), &filter, &start_polygon, nearest_start.data())));
    ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(end.data(),
        extents.data(), &filter, &end_polygon, nearest_end.data())));
    ASSERT_NE(start_polygon, 0u);
    ASSERT_NE(end_polygon, 0u);

    std::array<dtPolyRef, 32> corridor{};
    int corridor_count = 0;
    const dtStatus path_status = query->findPath(start_polygon, end_polygon,
        nearest_start.data(), nearest_end.data(), &filter, corridor.data(),
        &corridor_count, static_cast<int>(corridor.size()));
    ASSERT_TRUE(dtStatusSucceed(path_status));
    ASSERT_GT(corridor_count, 0);
    EXPECT_NE(corridor[static_cast<size_t>(corridor_count - 1)], end_polygon);
}

TEST(NavTileBuild, PreservesAuthoredSlopeHeightWithinCellHeight)
{
    nw::nav::NavGeometry slope;
    slope.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 1.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 1.0f},
    };
    slope.indices = {0, 2, 1, 1, 2, 3};
    slope.surface.assign(2, 1u);
    const std::array<uint32_t, 2> triangles{0u, 1u};
    const std::array<uint8_t, 2> walkable{0u, 1u};
    nw::nav::NavTileBuildConfig config;
    config.erosion_cells = 1;
    nw::nav::NavTileData tile_data;
    const nw::nav::NavTileBuildInput input{
        .surface_vertices = slope.vertices,
        .surface_indices = slope.indices,
        .surface_ids = slope.surface,
        .surface_triangles = triangles,
        .surface_walkable = walkable,
        .tile = {0, 0},
    };
    const auto build
        = nw::nav::build_nav_tile_data(input, config, tile_data);
    ASSERT_EQ(build.status, nw::nav::NavStatus::ok);
    ASSERT_GT(build.normalized_height_vertex_count, 0u);
    ASSERT_FALSE(tile_data.empty());

    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{
        dtAllocNavMesh(), dtFreeNavMesh};
    ASSERT_NE(mesh, nullptr);
    dtNavMeshParams params{};
    params.tileWidth = nw::nav::nav_tile_size;
    params.tileHeight = nw::nav::nav_tile_size;
    params.maxTiles = 1;
    params.maxPolys = 64;
    ASSERT_TRUE(dtStatusSucceed(mesh->init(&params)));
    const int payload_size = tile_data.size();
    unsigned char* payload = tile_data.release();
    if (dtStatusFailed(mesh->addTile(
            payload, payload_size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(payload);
        FAIL() << "failed to add slope test tile";
    }

    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
        dtAllocNavMeshQuery(), dtFreeNavMeshQuery};
    ASSERT_NE(query, nullptr);
    ASSERT_TRUE(dtStatusSucceed(query->init(mesh.get(), 64)));
    dtQueryFilter filter;
    filter.setIncludeFlags(nw::nav::nav_walkable_flag);
    const std::array<float, 3> extents{0.25f, 0.5f, 0.25f};
    for (float x : {1.0f, 3.0f, 5.0f, 7.0f, 9.0f}) {
        const std::array<float, 3> point{x, x * 0.1f, 5.0f};
        std::array<float, 3> nearest{};
        dtPolyRef polygon = 0;
        ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(point.data(),
            extents.data(), &filter, &polygon, nearest.data())));
        ASSERT_NE(polygon, 0u);
        EXPECT_NEAR(nearest[1], point[1], config.cell_height + 1.0e-4f);
    }
}

TEST(NavTileBuild, PreservesDistinctStackedFloorHeights)
{
    nw::nav::NavGeometry floors;
    floors.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
        {0.0f, 0.0f, 3.0f},
        {10.0f, 0.0f, 3.0f},
        {0.0f, 10.0f, 3.0f},
        {10.0f, 10.0f, 3.0f},
    };
    floors.indices = {0, 2, 1, 1, 2, 3, 4, 6, 5, 5, 6, 7};
    floors.surface.assign(4, 1u);
    const std::array<uint32_t, 4> triangles{0u, 1u, 2u, 3u};
    const std::array<uint8_t, 2> walkable{0u, 1u};
    nw::nav::NavTileBuildConfig config;
    config.erosion_cells = 1;
    nw::nav::NavTileData tile_data;
    const nw::nav::NavTileBuildInput input{
        .surface_vertices = floors.vertices,
        .surface_indices = floors.indices,
        .surface_ids = floors.surface,
        .surface_triangles = triangles,
        .surface_walkable = walkable,
        .tile = {0, 0},
    };
    const auto build
        = nw::nav::build_nav_tile_data(input, config, tile_data);
    ASSERT_EQ(build.status, nw::nav::NavStatus::ok);
    ASSERT_FALSE(tile_data.empty());

    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{
        dtAllocNavMesh(), dtFreeNavMesh};
    ASSERT_NE(mesh, nullptr);
    dtNavMeshParams params{};
    params.tileWidth = nw::nav::nav_tile_size;
    params.tileHeight = nw::nav::nav_tile_size;
    params.maxTiles = 1;
    params.maxPolys = 64;
    ASSERT_TRUE(dtStatusSucceed(mesh->init(&params)));
    const int payload_size = tile_data.size();
    unsigned char* payload = tile_data.release();
    if (dtStatusFailed(mesh->addTile(
            payload, payload_size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(payload);
        FAIL() << "failed to add stacked-floor test tile";
    }

    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
        dtAllocNavMeshQuery(), dtFreeNavMeshQuery};
    ASSERT_NE(query, nullptr);
    ASSERT_TRUE(dtStatusSucceed(query->init(mesh.get(), 64)));
    dtQueryFilter filter;
    filter.setIncludeFlags(nw::nav::nav_walkable_flag);
    const std::array<float, 3> extents{0.25f, 0.5f, 0.25f};
    std::array<dtPolyRef, 2> polygons{};
    for (size_t layer = 0; layer < polygons.size(); ++layer) {
        const float height = static_cast<float>(layer) * 3.0f;
        const std::array<float, 3> point{5.0f, height, 5.0f};
        std::array<float, 3> nearest{};
        ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(point.data(),
            extents.data(), &filter, &polygons[layer], nearest.data())));
        ASSERT_NE(polygons[layer], 0u);
        EXPECT_NEAR(nearest[1], height, config.cell_height + 1.0e-4f);
    }
    EXPECT_NE(polygons[0], polygons[1]);
}

TEST(NavTileBuild, RejectsDisabledDetailSampling)
{
    const auto geometry = make_two_tile_recast_floor();
    nw::nav::NavTileBuildConfig config;
    config.detail_sample_distance = 0.0f;
    const std::array<uint32_t, 2> triangles{0u, 1u};
    const std::array<uint8_t, 2> walkable{0u, 1u};
    const nw::nav::NavTileBuildInput input{
        .surface_vertices = geometry.vertices,
        .surface_indices = geometry.indices,
        .surface_ids = geometry.surface,
        .surface_triangles = triangles,
        .surface_walkable = walkable,
        .tile = {0, 0},
    };
    nw::nav::NavTileData tile_data;

    EXPECT_EQ(nw::nav::build_nav_tile_data(input, config, tile_data).status,
        nw::nav::NavStatus::rejected);
    EXPECT_TRUE(tile_data.empty());
}

TEST(NavTileBuild, RejectsUnrepresentableTileCoordinate)
{
    const auto geometry = make_two_tile_recast_floor();
    const std::array<uint32_t, 2> triangles{0u, 1u};
    const std::array<uint8_t, 2> walkable{0u, 1u};
    const nw::nav::NavTileBuildInput input{
        .surface_vertices = geometry.vertices,
        .surface_indices = geometry.indices,
        .surface_ids = geometry.surface,
        .surface_triangles = triangles,
        .surface_walkable = walkable,
        .tile = {std::numeric_limits<uint32_t>::max(), 0},
    };
    nw::nav::NavTileData tile_data;

    EXPECT_EQ(nw::nav::build_nav_tile_data(
                  input, nw::nav::NavTileBuildConfig{}, tile_data)
                  .status,
        nw::nav::NavStatus::rejected);
    EXPECT_TRUE(tile_data.empty());
}
