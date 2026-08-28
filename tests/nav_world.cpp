#include <gtest/gtest.h>

#include <nw/nav/NavTileBuild.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

nw::nav::NavAreaBuildSource make_floor(uint32_t width, uint32_t height)
{
    nw::nav::NavAreaBuildSource source;
    source.surface_walkable = {0u, 1u, 0u};
    source.width = width;
    source.height = height;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
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

void append_wall(nw::nav::NavAreaBuildSource& source, float x,
    float minimum_y, float maximum_y, uint32_t state)
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
        source.obstacle_surface_ids.end(), {2u, 2u});
    source.obstacle_owner.insert(source.obstacle_owner.end(), {state, state});
    source.obstacle_state_count
        = std::max(source.obstacle_state_count, state + 1);
}

nw::nav::NavStatus build_world(const nw::nav::NavAreaBuildSource& source,
    std::span<const uint8_t> active, nw::nav::NavWorldState& world,
    nw::nav::NavTiledWorldBuildStats& stats, uint16_t erosion_cells = 1)
{
    nw::nav::NavTileBuildConfig config;
    config.erosion_cells = erosion_cells;
    return nw::nav::build_tiled_nav_world(
        source, active, config, world, stats);
}

} // namespace

TEST(NavWorld, FindsPathAndMovesAcrossGeneratedTileSeam)
{
    const auto source = make_floor(2, 1);
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(build_world(source, {}, world, build), nw::nav::NavStatus::ok);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavPathResult, 1> path{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, path);
    ASSERT_EQ(path[0].status, nw::nav::NavStatus::ok);
    ASSERT_EQ(path[0].corner_count, 2u);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{{1.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(
                  world, registrations, registered)
                  .output_count,
        1u);
    const std::array movements{nw::nav::NavAgentMotionInput{
        registered[0].agent,
        registered[0].position,
        {18.0f, 0.0f, 0.0f},
    }};
    std::array<nw::nav::NavAgentMotionResult, 1> moved{};
    ASSERT_EQ(nw::nav::move_nav_agents(world, movements, moved).output_count,
        1u);
    EXPECT_GT(moved[0].position.x, 10.0f);
}

TEST(NavWorld, ClampsDestinationBeyondGeneratedArea)
{
    const auto source = make_floor(2, 1);
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(build_world(source, {}, world, build), nw::nav::NavStatus::ok);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {40.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, results);

    ASSERT_EQ(results[0].status, nw::nav::NavStatus::clamped);
    ASSERT_GT(results[0].corner_count, 0u);
    const auto& endpoint = corners[results[0].corner_offset
        + results[0].corner_count - 1];
    EXPECT_GT(endpoint.x, 1.0f);
    EXPECT_LT(endpoint.x, 20.01f);
}

TEST(NavWorld, RegistersMovesAndReleasesAgentsAsBatches)
{
    const auto source = make_floor(2, 1);
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(build_world(source, {}, world, build), nw::nav::NavStatus::ok);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{{1.0f, 3.0f, 0.0f}},
        nw::nav::NavAgentRegistrationInput{{19.0f, 7.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 2> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(
                  world, registrations, registered)
                  .output_count,
        2u);

    const std::array movements{
        nw::nav::NavAgentMotionInput{
            registered[0].agent, registered[0].position, {2.0f, 0.0f, 0.0f}},
        nw::nav::NavAgentMotionInput{
            registered[1].agent, registered[1].position, {20.0f, 0.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentMotionResult, 2> moved{};
    const auto movement = nw::nav::move_nav_agents(world, movements, moved);
    EXPECT_EQ(movement.output_count, 2u);
    EXPECT_NEAR(moved[0].position.x, 3.0f, 0.02f);
    EXPECT_EQ(moved[1].status, nw::nav::NavStatus::clamped);

    const std::array agents{registered[0].agent, registered[1].agent};
    EXPECT_EQ(nw::nav::release_nav_agents(world, agents).output_count, 2u);
    EXPECT_EQ(nw::nav::release_nav_agents(world, agents).rejected_count, 2u);
}

TEST(NavWorld, ResolvesMovementHeightOnAuthoredSlope)
{
    auto source = make_floor(1, 1);
    source.surface_vertices[1].z = 2.0f;
    source.surface_vertices[3].z = 2.0f;
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(build_world(source, {}, world, build, 0),
        nw::nav::NavStatus::ok);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{{1.0f, 5.0f, 0.2f}},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(
                  world, registrations, registered)
                  .output_count,
        1u);
    glm::vec3 position = registered[0].position;
    for (size_t step = 0; step < 80; ++step) {
        const std::array movement{nw::nav::NavAgentMotionInput{
            registered[0].agent, position, {0.1f, 0.0f, 0.0f}}};
        std::array<nw::nav::NavAgentMotionResult, 1> result{};
        ASSERT_EQ(nw::nav::move_nav_agents(world, movement, result).output_count,
            1u);
        position = result[0].position;
    }
    EXPECT_NEAR(position.x, 9.0f, 0.03f);
    EXPECT_NEAR(position.z, 1.8f, 0.11f);
}

TEST(NavWorld, ProjectsRayToNearestStackedGeneratedSurface)
{
    auto source = make_floor(1, 1);
    const uint32_t vertex
        = static_cast<uint32_t>(source.surface_vertices.size());
    source.surface_vertices.insert(source.surface_vertices.end(), {
                                                                      {0.0f, 0.0f, 3.0f},
                                                                      {10.0f, 0.0f, 3.0f},
                                                                      {0.0f, 10.0f, 3.0f},
                                                                      {10.0f, 10.0f, 3.0f},
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
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(build_world(source, {}, world, build, 0),
        nw::nav::NavStatus::ok);

    constexpr std::array rays{nw::nav::NavRayProjectionInput{
        {5.0f, 5.0f, 10.0f}, {0.0f, 0.0f, -20.0f}}};
    std::array<nw::nav::NavRayProjectionResult, 1> result{};
    ASSERT_EQ(nw::nav::project_nav_rays(world, rays, result).output_count, 1u);
    EXPECT_EQ(result[0].status, nw::nav::NavStatus::ok);
    EXPECT_NEAR(result[0].position.z, 3.0f, 0.11f);
}

TEST(NavWorld, DebugBatchReportsAndRemovesActiveObstacleGeometry)
{
    auto source = make_floor(1, 1);
    append_wall(source, 5.0f, 0.0f, 10.0f, 0);
    constexpr std::array<uint8_t, 1> active{1u};
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    ASSERT_EQ(build_world(source, active, world, build),
        nw::nav::NavStatus::ok);

    nw::Vector<nw::nav::NavDebugTriangle> triangles;
    nw::nav::collect_nav_debug_triangles(world, triangles);
    EXPECT_EQ(std::ranges::count_if(triangles, [](const auto& triangle) {
        return triangle.state == nw::nav::NavDebugPolygonState::blocked;
    }),
        2);

    constexpr std::array changes{
        nw::nav::NavObstacleStateChange{.obstacle_state = 0, .active = 0},
    };
    std::array<uint32_t, 1> rebuilt{};
    nw::nav::NavTileRebuildStats rebuild;
    ASSERT_EQ(nw::nav::rebuild_nav_tiles(
                  world, source, changes, rebuilt, rebuild),
        nw::nav::NavStatus::ok);
    nw::nav::collect_nav_debug_triangles(world, triangles);
    EXPECT_EQ(std::ranges::count_if(triangles, [](const auto& triangle) {
        return triangle.state == nw::nav::NavDebugPolygonState::blocked;
    }),
        0);
}

TEST(NavWorld, RejectsMalformedMovementBatchRowsExplicitly)
{
    nw::nav::NavWorldState world;
    constexpr std::array inputs{nw::nav::NavAgentMotionInput{
        0, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}};
    std::array<nw::nav::NavAgentMotionResult, 1> results{};
    const auto stats = nw::nav::move_nav_agents(world, inputs, results);
    EXPECT_EQ(stats.rejected_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::rejected);
}
