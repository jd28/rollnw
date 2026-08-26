#include <gtest/gtest.h>

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavWorld.hpp>
#include <nw/objects/Area.hpp>

#include <array>
#include <ranges>

namespace {

nw::nav::NavGeometry make_two_tile_floor()
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
    geometry.surface.assign(4, 1);
    geometry.kind.assign(4, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner = {0, 0, 1, 1};
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_disconnected_two_tile_floor()
{
    auto geometry = make_two_tile_floor();
    for (size_t index = 4; index < geometry.vertices.size(); ++index)
        geometry.vertices[index].x += 1.0f;
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_height_separated_two_tile_floor(float height)
{
    auto geometry = make_two_tile_floor();
    for (size_t index = 4; index < geometry.vertices.size(); ++index)
        geometry.vertices[index].z += height;
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_counterclockwise_two_tile_floor()
{
    auto geometry = make_two_tile_floor();
    for (size_t triangle = 0; triangle < geometry.triangle_count(); ++triangle) {
        std::swap(geometry.indices[triangle * 3 + 1], geometry.indices[triangle * 3 + 2]);
    }
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_mixed_winding_two_tile_floor()
{
    auto geometry = make_two_tile_floor();
    for (size_t triangle = 0; triangle < geometry.triangle_count(); triangle += 2) {
        std::swap(geometry.indices[triangle * 3 + 1], geometry.indices[triangle * 3 + 2]);
    }
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_subdivided_two_tile_floor()
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {20.0f, 0.0f, 0.0f},
        {10.0f, 5.0f, 0.0f},
        {20.0f, 5.0f, 0.0f},
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
        6,
        8,
        7,
        7,
        8,
        9,
    };
    geometry.surface.assign(6, 1);
    geometry.kind.assign(6, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner = {0, 0, 1, 1, 1, 1};
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_l_shaped_floor()
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {7.0f, 0.0f, 0.0f},
        {0.0f, 3.0f, 0.0f},
        {7.0f, 3.0f, 0.0f},
        {7.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {7.0f, 3.0f, 0.0f},
        {10.0f, 3.0f, 0.0f},
        {7.0f, 3.0f, 0.0f},
        {10.0f, 3.0f, 0.0f},
        {7.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
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
        8,
        10,
        9,
        9,
        10,
        11,
    };
    geometry.surface.assign(6, 1);
    geometry.kind.assign(6, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner.assign(6, 0);
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_square_obstacle_floor()
{
    nw::nav::NavGeometry geometry;
    constexpr std::array coordinates{0.0f, 4.0f, 6.0f, 10.0f};
    for (float y : coordinates) {
        for (float x : coordinates) {
            geometry.vertices.emplace_back(x, y, 0.0f);
        }
    }
    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 3; ++x) {
            if (x == 1 && y == 1) continue;
            const uint32_t lower_left = y * 4 + x;
            const uint32_t lower_right = lower_left + 1;
            const uint32_t upper_left = lower_left + 4;
            const uint32_t upper_right = upper_left + 1;
            geometry.indices.insert(geometry.indices.end(), {
                                                                lower_left,
                                                                upper_left,
                                                                lower_right,
                                                                lower_right,
                                                                upper_left,
                                                                upper_right,
                                                            });
        }
    }
    geometry.surface.assign(geometry.triangle_count(), 1);
    geometry.kind.assign(geometry.triangle_count(), nw::nav::NavGeometryKind::tile_wok);
    geometry.owner.assign(geometry.triangle_count(), 0);
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_sloped_floor()
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 2.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 2.0f},
    };
    geometry.indices = {0, 2, 1, 1, 2, 3};
    geometry.surface.assign(2, 1);
    geometry.kind.assign(2, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner.assign(2, 0);
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

nw::nav::NavGeometry make_stacked_floor()
{
    nw::nav::NavGeometry geometry;
    geometry.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
        {0.0f, 0.0f, 3.0f},
        {10.0f, 0.0f, 3.0f},
        {0.0f, 10.0f, 3.0f},
        {10.0f, 10.0f, 3.0f},
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
    geometry.surface.assign(4, 1);
    geometry.kind.assign(4, nw::nav::NavGeometryKind::tile_wok);
    geometry.owner.assign(4, 0);
    nw::nav::build_nav_geometry_adjacency(geometry);
    return geometry;
}

} // namespace

TEST(NavWorld, BuildsAndFindsPathAcrossNwnTileSeam)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;

    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build_stats.tile_count, 2u);
    EXPECT_EQ(build_stats.polygon_count, 4u);
    EXPECT_EQ(build_stats.portal_edge_count, 2u);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    const auto stats = nw::nav::find_nav_paths(world, requests, corners, results);

    EXPECT_EQ(stats.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_EQ(results[0].corner_count, 2u);
    const uint32_t begin = results[0].corner_offset;
    const uint32_t end = begin + results[0].corner_count;
    EXPECT_NEAR(corners[begin].x, 1.0f, 0.01f);
    EXPECT_NEAR(corners[end - 1].x, 19.0f, 0.01f);
    for (uint32_t corner = begin; corner < end; ++corner) {
        EXPECT_NEAR(corners[corner].y, 5.0f, 0.01f);
    }
}

TEST(NavWorld, ClampsClickInsideNonWalkableGeometryToReachableBoundary)
{
    auto geometry = make_square_obstacle_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr float clearance = 0.4f;
    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.5f, 5.0f, 0.0f}, {5.0f, 5.0f, 0.0f}, clearance},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    const auto stats = nw::nav::find_nav_paths(world, requests, corners, results);

    EXPECT_EQ(stats.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::clamped);
    ASSERT_GT(results[0].corner_count, 1u);
    const auto& last = corners[results[0].corner_offset + results[0].corner_count - 1];
    EXPECT_GT(last.x, 1.5f);
    EXPECT_LE(last.x, 4.0f - clearance + 0.01f);
    EXPECT_NEAR(last.y, 5.0f, 0.01f);
}

TEST(NavWorld, ClampsClickBeyondNavmeshToFurthestReachableBoundary)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr float clearance = 0.4f;
    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {40.0f, 5.0f, 0.0f}, clearance},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    const auto stats = nw::nav::find_nav_paths(world, requests, corners, results);

    EXPECT_EQ(stats.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::clamped);
    ASSERT_GT(results[0].corner_count, 1u);
    const auto& last = corners[results[0].corner_offset + results[0].corner_count - 1];
    EXPECT_GT(last.x, 1.0f);
    EXPECT_LE(last.x, 20.0f - clearance + 0.01f);
    EXPECT_NEAR(last.y, 5.0f, 0.01f);
}

TEST(NavWorld, DoesNotBuildPortalAcrossHeightSeparatedTileSeam)
{
    auto geometry = make_height_separated_two_tile_floor(0.75f);
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;

    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build_stats.tile_count, 2u);
    EXPECT_EQ(build_stats.portal_edge_count, 0u);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.75f}},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, results);

    ASSERT_GT(results[0].corner_count, 0u);
    const auto& last = corners[results[0].corner_offset + results[0].corner_count - 1];
    EXPECT_LE(last.x, 10.0f);
}

TEST(NavWorld, BuildsPortalAcrossClimbableTileSeam)
{
    auto geometry = make_height_separated_two_tile_floor(0.25f);
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;

    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build_stats.portal_edge_count, 2u);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.25f}},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, results);

    ASSERT_GT(results[0].corner_count, 0u);
    const auto& last = corners[results[0].corner_offset + results[0].corner_count - 1];
    EXPECT_NEAR(last.x, 19.0f, 0.01f);
    EXPECT_NEAR(last.z, 0.25f, 0.01f);
}

TEST(NavWorld, BuildsPortalAcrossDifferentlySubdividedTileEdges)
{
    auto geometry = make_subdivided_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;

    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build_stats.portal_edge_count, 3u);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 2.5f, 0.0f}, {19.0f, 2.5f, 0.0f}},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    const auto stats = nw::nav::find_nav_paths(world, requests, corners, results);

    EXPECT_EQ(stats.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_EQ(results[0].corner_count, 2u);
    const uint32_t begin = results[0].corner_offset;
    const uint32_t end = begin + results[0].corner_count;
    EXPECT_NEAR(corners[begin].x, 1.0f, 0.01f);
    EXPECT_NEAR(corners[end - 1].x, 19.0f, 0.01f);
    for (uint32_t corner = begin; corner < end; ++corner) {
        EXPECT_NEAR(corners[corner].y, 2.5f, 0.01f);
    }
}

TEST(NavWorld, NormalizesCounterclockwiseAndMixedPolygonWinding)
{
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    for (auto geometry : {
             make_counterclockwise_two_tile_floor(),
             make_mixed_winding_two_tile_floor(),
         }) {
        nw::nav::NavWorldState world;
        nw::nav::NavBuildStats build_stats;
        ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
            nw::nav::NavStatus::ok);

        constexpr std::array requests{
            nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.0f}},
        };
        std::array<nw::nav::NavPathResult, 1> results{};
        nw::Vector<glm::vec3> corners;
        nw::nav::find_nav_paths(world, requests, corners, results);

        ASSERT_EQ(results[0].status, nw::nav::NavStatus::ok);
        ASSERT_EQ(results[0].corner_count, 2u);
        const size_t begin = results[0].corner_offset;
        EXPECT_NEAR(corners[begin].x, 1.0f, 0.01f);
        EXPECT_NEAR(corners[begin].y, 5.0f, 0.01f);
        EXPECT_NEAR(corners[begin + 1].x, 19.0f, 0.01f);
        EXPECT_NEAR(corners[begin + 1].y, 5.0f, 0.01f);
    }
}

TEST(NavWorld, FunnelUsesOnlyTheNecessaryCornerAroundAnObstacle)
{
    auto geometry = make_l_shaped_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr glm::vec3 start{1.5f, 1.5f, 0.0f};
    constexpr glm::vec3 end{8.5f, 8.5f, 0.0f};
    constexpr std::array requests{nw::nav::NavPathRequest{start, end}};
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, results);

    ASSERT_EQ(results[0].status, nw::nav::NavStatus::ok);
    ASSERT_EQ(results[0].corner_count, 3u);
    const size_t begin = results[0].corner_offset;
    EXPECT_NEAR(glm::distance(corners[begin], start), 0.0f, 0.01f);
    EXPECT_NEAR(corners[begin + 1].x, 7.0f, 0.01f);
    EXPECT_NEAR(corners[begin + 1].y, 3.0f, 0.01f);
    EXPECT_NEAR(glm::distance(corners[begin + 2], end), 0.0f, 0.01f);
}

TEST(NavWorld, RepeatedMovementFollowsFunnelAroundCornerWithoutBacktracking)
{
    auto geometry = make_l_shaped_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr glm::vec3 start{1.5f, 1.5f, 0.0f};
    constexpr glm::vec3 end{8.5f, 8.5f, 0.0f};
    constexpr std::array requests{nw::nav::NavPathRequest{start, end}};
    std::array<nw::nav::NavPathResult, 1> path_results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, path_results);
    ASSERT_EQ(path_results[0].status, nw::nav::NavStatus::ok);

    constexpr std::array registrations{nw::nav::NavAgentRegistrationInput{start}};
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(world, registrations, registered).output_count, 1u);

    glm::vec3 position = registered[0].position;
    size_t corner = path_results[0].corner_offset + 1;
    const size_t corner_end = path_results[0].corner_offset + path_results[0].corner_count;
    float previous_distance = std::numeric_limits<float>::max();
    for (size_t step = 0; step < 500 && corner < corner_end; ++step) {
        glm::vec3 remaining = corners[corner] - position;
        remaining.z = 0.0f;
        const float distance = glm::length(glm::vec2{remaining.x, remaining.y});
        if (distance <= 0.05f) {
            ++corner;
            previous_distance = std::numeric_limits<float>::max();
            continue;
        }
        EXPECT_LE(distance, previous_distance + 0.001f);
        previous_distance = distance;
        const glm::vec3 desired = remaining / distance * std::min(distance, 0.05f);
        const std::array movements{
            nw::nav::NavAgentMotionInput{registered[0].agent, position, desired},
        };
        std::array<nw::nav::NavAgentMotionResult, 1> moved{};
        ASSERT_EQ(nw::nav::move_nav_agents(world, movements, moved).output_count, 1u);
        ASSERT_TRUE(moved[0].status == nw::nav::NavStatus::ok
            || moved[0].status == nw::nav::NavStatus::clamped);
        position = moved[0].position;
    }

    EXPECT_EQ(corner, corner_end);
    EXPECT_LT(glm::length(glm::vec2{position.x - end.x, position.y - end.y}), 0.051f);
}

TEST(NavWorld, ClearanceAgentFollowsFunnelAroundWallCorner)
{
    auto geometry = make_l_shaped_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr float clearance = 0.3f;
    constexpr glm::vec3 start{1.5f, 1.5f, 0.0f};
    constexpr glm::vec3 end{8.5f, 8.5f, 0.0f};
    constexpr std::array requests{nw::nav::NavPathRequest{start, end, clearance}};
    std::array<nw::nav::NavPathResult, 1> path_results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, path_results);
    ASSERT_EQ(path_results[0].status, nw::nav::NavStatus::clamped);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{start, clearance},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(world, registrations, registered).output_count, 1u);

    glm::vec3 position = registered[0].position;
    size_t corner = path_results[0].corner_offset + 1;
    const size_t corner_end = path_results[0].corner_offset + path_results[0].corner_count;
    for (size_t step = 0; step < 500 && corner < corner_end; ++step) {
        glm::vec3 remaining = corners[corner] - position;
        remaining.z = 0.0f;
        const float distance = glm::length(glm::vec2{remaining.x, remaining.y});
        if (distance <= 0.05f) {
            ++corner;
            continue;
        }
        const glm::vec3 desired = remaining / distance * std::min(distance, 0.05f);
        const std::array movements{
            nw::nav::NavAgentMotionInput{registered[0].agent, position, desired},
        };
        std::array<nw::nav::NavAgentMotionResult, 1> moved{};
        ASSERT_EQ(nw::nav::move_nav_agents(world, movements, moved).output_count, 1u);
        ASSERT_TRUE(moved[0].status == nw::nav::NavStatus::ok
            || moved[0].status == nw::nav::NavStatus::clamped);
        position = moved[0].position;
    }

    EXPECT_EQ(corner, corner_end);
    EXPECT_LT(glm::length(glm::vec2{position.x - end.x, position.y - end.y}), 0.051f);
}

TEST(NavWorld, ClearanceAgentDoesNotStallAtSquareObstacleCorners)
{
    auto geometry = make_square_obstacle_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr float clearance = 0.3f;
    constexpr glm::vec3 start{1.5f, 5.0f, 0.0f};
    constexpr glm::vec3 end{8.5f, 5.0f, 0.0f};
    constexpr std::array requests{nw::nav::NavPathRequest{start, end, clearance}};
    std::array<nw::nav::NavPathResult, 1> path_results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, path_results);
    ASSERT_TRUE(path_results[0].status == nw::nav::NavStatus::ok
        || path_results[0].status == nw::nav::NavStatus::clamped);
    ASSERT_GT(path_results[0].corner_count, 2u);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{start, clearance},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(world, registrations, registered).output_count, 1u);

    glm::vec3 position = registered[0].position;
    size_t corner = path_results[0].corner_offset + 1;
    const size_t corner_end = path_results[0].corner_offset + path_results[0].corner_count;
    size_t stalled_steps = 0;
    for (size_t step = 0; step < 500 && corner < corner_end; ++step) {
        glm::vec3 remaining = corners[corner] - position;
        remaining.z = 0.0f;
        const float distance = glm::length(glm::vec2{remaining.x, remaining.y});
        if (distance <= 0.05f) {
            ++corner;
            continue;
        }

        const glm::vec3 desired = remaining / distance * std::min(distance, 0.05f);
        const std::array movements{
            nw::nav::NavAgentMotionInput{registered[0].agent, position, desired},
        };
        std::array<nw::nav::NavAgentMotionResult, 1> moved{};
        ASSERT_EQ(nw::nav::move_nav_agents(world, movements, moved).output_count, 1u);
        ASSERT_TRUE(moved[0].status == nw::nav::NavStatus::ok
            || moved[0].status == nw::nav::NavStatus::clamped);
        const glm::vec2 applied{
            moved[0].position.x - position.x,
            moved[0].position.y - position.y,
        };
        stalled_steps += static_cast<size_t>(glm::dot(applied, applied) <= 1.0e-8f);
        position = moved[0].position;
    }

    EXPECT_EQ(corner, corner_end)
        << " position=" << position.x << ',' << position.y
        << " corners=" << corners[path_results[0].corner_offset].x << ','
        << corners[path_results[0].corner_offset].y << " -> "
        << corners[path_results[0].corner_offset + 1].x << ','
        << corners[path_results[0].corner_offset + 1].y << " -> "
        << corners[path_results[0].corner_offset + 2].x << ','
        << corners[path_results[0].corner_offset + 2].y << " -> "
        << corners[path_results[0].corner_offset + 3].x << ','
        << corners[path_results[0].corner_offset + 3].y;
    EXPECT_EQ(stalled_steps, 0u);
    EXPECT_LT(glm::length(glm::vec2{position.x - end.x, position.y - end.y}), 0.051f);
}

TEST(NavWorld, HorizontalMovementResolvesHeightOnSlopedSurface)
{
    auto geometry = make_sloped_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr glm::vec3 start{1.0f, 5.0f, 0.2f};
    constexpr glm::vec3 end{9.0f, 5.0f, 1.8f};
    constexpr std::array requests{nw::nav::NavPathRequest{start, end}};
    std::array<nw::nav::NavPathResult, 1> path_results{};
    nw::Vector<glm::vec3> corners;
    nw::nav::find_nav_paths(world, requests, corners, path_results);
    ASSERT_EQ(path_results[0].status, nw::nav::NavStatus::ok);
    ASSERT_EQ(path_results[0].corner_count, 2u);

    constexpr std::array registrations{nw::nav::NavAgentRegistrationInput{start}};
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(world, registrations, registered).output_count, 1u);
    glm::vec3 position = registered[0].position;
    for (size_t step = 0; step < 80; ++step) {
        const std::array movements{nw::nav::NavAgentMotionInput{
            registered[0].agent, position, {0.1f, 0.0f, 0.0f}}};
        std::array<nw::nav::NavAgentMotionResult, 1> moved{};
        ASSERT_EQ(nw::nav::move_nav_agents(world, movements, moved).output_count, 1u);
        position = moved[0].position;
    }

    EXPECT_NEAR(position.x, end.x, 0.02f);
    EXPECT_NEAR(position.y, end.y, 0.02f);
    EXPECT_NEAR(position.z, end.z, 0.02f);
}

TEST(NavWorld, BuildsPortalAcrossRepeatedDedicatedServerWok)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.push_back({"tall_a01_01"});

    nw::Area area;
    area.width = 2;
    area.height = 1;
    area.tileset = &tileset;
    area.tiles.resize(2);

    nw::nav::NavGeometry geometry;
    const auto geometry_stats = nw::nav::build_area_tile_nav_geometry(
        area, nw::kernel::resman(), geometry);
    ASSERT_EQ(geometry_stats.wok_tile_count, 2u);
    ASSERT_GT(geometry.adjacency.size(), 0u);

    const auto* surfaces = nw::kernel::twodas().get("surfacemat");
    ASSERT_NE(surfaces, nullptr);
    nw::Vector<uint8_t> walkable;
    ASSERT_GT(nw::nav::build_nav_surface_walkability(*surfaces, walkable).walkable_count, 0u);

    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    EXPECT_GT(build_stats.portal_edge_count, 0u);

    size_t seam_edge = geometry.adjacency.size();
    for (size_t edge = 0; edge < geometry.adjacency.size(); ++edge) {
        const int32_t neighbor = geometry.adjacency[edge];
        if (neighbor >= 0
            && geometry.owner[edge / 3] != geometry.owner[static_cast<size_t>(neighbor)]) {
            seam_edge = edge;
            break;
        }
    }
    ASSERT_LT(seam_edge, geometry.adjacency.size());
    const size_t first_triangle = seam_edge / 3;
    const size_t second_triangle = static_cast<size_t>(geometry.adjacency[seam_edge]);
    const auto centroid = [&](size_t triangle) {
        const size_t offset = triangle * 3;
        return (geometry.vertices[geometry.indices[offset]]
                   + geometry.vertices[geometry.indices[offset + 1]]
                   + geometry.vertices[geometry.indices[offset + 2]])
            / 3.0f;
    };
    const glm::vec3 start = centroid(first_triangle);
    const glm::vec3 end = centroid(second_triangle);
    const std::array requests{nw::nav::NavPathRequest{start, end}};
    std::array<nw::nav::NavPathResult, 1> path_results{};
    nw::Vector<glm::vec3> corners;
    const auto path_stats = nw::nav::find_nav_paths(world, requests, corners, path_results);
    EXPECT_EQ(path_stats.output_count, 1u);
    EXPECT_EQ(path_results[0].status, nw::nav::NavStatus::ok);

    const std::array registrations{nw::nav::NavAgentRegistrationInput{start}};
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(world, registrations, registered).output_count, 1u);
    const std::array movements{nw::nav::NavAgentMotionInput{
        registered[0].agent, registered[0].position, end - registered[0].position}};
    std::array<nw::nav::NavAgentMotionResult, 1> moved{};
    ASSERT_EQ(nw::nav::move_nav_agents(world, movements, moved).output_count, 1u);
    EXPECT_LT(glm::distance(moved[0].position, end), glm::distance(start, end));
}

TEST(NavWorld, DoesNotOpenPortalAcrossUnmatchedTileEdges)
{
    auto geometry = make_disconnected_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;

    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    EXPECT_EQ(build_stats.tile_count, 2u);
    EXPECT_EQ(build_stats.portal_edge_count, 0u);

    constexpr std::array requests{
        nw::nav::NavPathRequest{{1.0f, 5.0f, 0.0f}, {19.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavPathResult, 1> results{};
    nw::Vector<glm::vec3> corners;
    const auto stats = nw::nav::find_nav_paths(world, requests, corners, results);

    ASSERT_EQ(stats.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::clamped);
    ASSERT_GT(results[0].corner_count, 0u);
    const auto& last = corners[results[0].corner_offset + results[0].corner_count - 1];
    EXPECT_LE(last.x, 10.01f);
}

TEST(NavWorld, RegistersMovesAndReleasesAgentsAsBatches)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{{1.0f, 5.0f, 0.0f}},
        nw::nav::NavAgentRegistrationInput{{19.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 2> registered{};
    const auto registration_stats = nw::nav::register_nav_agents(world, registrations, registered);
    ASSERT_EQ(registration_stats.output_count, 2u);

    const std::array movements{
        nw::nav::NavAgentMotionInput{registered[0].agent, registered[0].position, {2.0f, 0.0f, 0.0f}},
        nw::nav::NavAgentMotionInput{registered[1].agent, registered[1].position, {20.0f, 0.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentMotionResult, 2> moved{};
    const auto movement_stats = nw::nav::move_nav_agents(world, movements, moved);

    EXPECT_EQ(movement_stats.output_count, 2u);
    EXPECT_NEAR(moved[0].position.x, 3.0f, 0.01f);
    EXPECT_EQ(moved[1].status, nw::nav::NavStatus::clamped);
    EXPECT_LE(moved[1].position.x, 20.01f);

    glm::vec3 repeated_position = moved[0].position;
    for (size_t step = 0; step < 40; ++step) {
        const std::array repeated_movements{
            nw::nav::NavAgentMotionInput{
                registered[0].agent, repeated_position, {0.25f, 0.0f, 0.0f}},
        };
        std::array<nw::nav::NavAgentMotionResult, 1> repeated_results{};
        const auto repeated_stats = nw::nav::move_nav_agents(
            world, repeated_movements, repeated_results);
        ASSERT_EQ(repeated_stats.output_count, 1u);
        repeated_position = repeated_results[0].position;
    }
    EXPECT_GT(repeated_position.x, 10.0f);

    const std::array agents{registered[0].agent, registered[1].agent};
    const auto release_stats = nw::nav::release_nav_agents(world, agents);
    EXPECT_EQ(release_stats.output_count, 2u);
}

TEST(NavWorld, KeepsAgentClearanceFromWallsAndCorners)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr float clearance = 0.3f;
    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{{0.1f, 0.1f, 0.0f}, clearance},
        nw::nav::NavAgentRegistrationInput{{1.0f, 5.0f, 0.0f}, clearance},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 2> registered{};
    const auto registration_stats = nw::nav::register_nav_agents(
        world, registrations, registered);
    ASSERT_EQ(registration_stats.output_count, 2u);
    EXPECT_EQ(registered[0].status, nw::nav::NavStatus::clamped);
    EXPECT_GE(registered[0].position.x, clearance - 0.001f);
    EXPECT_GE(registered[0].position.y, clearance - 0.001f);

    const std::array movements{
        nw::nav::NavAgentMotionInput{
            registered[0].agent, registered[0].position, {-2.0f, -2.0f, 0.0f}},
        nw::nav::NavAgentMotionInput{
            registered[1].agent, registered[1].position, {-2.0f, 0.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentMotionResult, 2> moved{};
    const auto movement_stats = nw::nav::move_nav_agents(world, movements, moved);
    ASSERT_EQ(movement_stats.output_count, 2u);
    EXPECT_GE(moved[0].position.x, clearance - 0.001f);
    EXPECT_GE(moved[0].position.y, clearance - 0.001f);
    EXPECT_GE(moved[1].position.x, clearance - 0.001f);
    EXPECT_NE(moved[0].status, nw::nav::NavStatus::ok);
    EXPECT_NE(moved[1].status, nw::nav::NavStatus::ok);
}

TEST(NavWorld, RejectsInvalidClearances)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    const std::array registrations{
        nw::nav::NavAgentRegistrationInput{{1.0f, 1.0f, 0.0f}, -0.1f},
        nw::nav::NavAgentRegistrationInput{
            {1.0f, 1.0f, 0.0f}, std::numeric_limits<float>::quiet_NaN()},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 2> registered{};
    const auto stats = nw::nav::register_nav_agents(world, registrations, registered);

    EXPECT_EQ(stats.output_count, 0u);
    EXPECT_EQ(stats.rejected_count, 2u);
    EXPECT_EQ(registered[0].status, nw::nav::NavStatus::rejected);
    EXPECT_EQ(registered[1].status, nw::nav::NavStatus::rejected);

    const std::array paths{
        nw::nav::NavPathRequest{{1.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 0.0f}, -0.1f},
        nw::nav::NavPathRequest{{1.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 0.0f},
            std::numeric_limits<float>::quiet_NaN()},
    };
    std::array<nw::nav::NavPathResult, 2> path_results{};
    nw::Vector<glm::vec3> corners;
    const auto path_stats = nw::nav::find_nav_paths(world, paths, corners, path_results);
    EXPECT_EQ(path_stats.output_count, 0u);
    EXPECT_EQ(path_stats.rejected_count, 2u);
    EXPECT_EQ(path_results[0].status, nw::nav::NavStatus::rejected);
    EXPECT_EQ(path_results[1].status, nw::nav::NavStatus::rejected);
    EXPECT_TRUE(corners.empty());
}

TEST(NavWorld, RejectsMalformedBatchRowsExplicitly)
{
    nw::nav::NavWorldState world;
    const std::array inputs{
        nw::nav::NavAgentMotionInput{0, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentMotionResult, 1> results{};
    const auto stats = nw::nav::move_nav_agents(world, inputs, results);

    EXPECT_EQ(stats.rejected_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::rejected);
}

TEST(NavWorld, KeepsOverlappingClosedBlockersComposed)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr std::array overlaps{
        nw::nav::NavBlockerOverlapInput{10, 2},
        nw::nav::NavBlockerOverlapInput{10, 3},
        nw::nav::NavBlockerOverlapInput{20, 2},
        nw::nav::NavBlockerOverlapInput{20, 3},
        nw::nav::NavBlockerOverlapInput{20, 3},
        nw::nav::NavBlockerOverlapInput{30, UINT32_MAX},
    };
    const auto blocker_stats = nw::nav::configure_nav_blockers(world, overlaps);
    EXPECT_EQ(blocker_stats.owner_count, 2u);
    EXPECT_EQ(blocker_stats.overlap_count, 4u);
    EXPECT_EQ(blocker_stats.duplicate_count, 1u);
    EXPECT_EQ(blocker_stats.rejected_count, 1u);

    constexpr std::array registrations{
        nw::nav::NavAgentRegistrationInput{{1.0f, 5.0f, 0.0f}},
    };
    std::array<nw::nav::NavAgentRegistrationResult, 1> registered{};
    ASSERT_EQ(nw::nav::register_nav_agents(world, registrations, registered).output_count, 1u);

    const auto move_across_seam = [&] {
        const std::array movements{
            nw::nav::NavAgentMotionInput{
                registered[0].agent, registered[0].position, {18.0f, 0.0f, 0.0f}},
        };
        std::array<nw::nav::NavAgentMotionResult, 1> moved{};
        nw::nav::move_nav_agents(world, movements, moved);
        return moved[0];
    };

    EXPECT_NE(move_across_seam().status, nw::nav::NavStatus::ok);
    constexpr std::array open_first{nw::nav::NavBlockerStateInput{10, false}};
    EXPECT_EQ(nw::nav::set_nav_blockers_closed(world, open_first).output_count, 1u);
    EXPECT_NE(move_across_seam().status, nw::nav::NavStatus::ok);

    constexpr std::array open_second{nw::nav::NavBlockerStateInput{20, false}};
    EXPECT_EQ(nw::nav::set_nav_blockers_closed(world, open_second).output_count, 1u);
    EXPECT_EQ(move_across_seam().status, nw::nav::NavStatus::ok);
}

TEST(NavWorld, CollectsBackendNeutralDebugTrianglesAndBlockerState)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    nw::Vector<nw::nav::NavDebugTriangle> triangles;
    const auto initial = nw::nav::collect_nav_debug_triangles(world, triangles);
    ASSERT_EQ(initial.input_count, 4u);
    ASSERT_EQ(initial.output_count, 4u);
    ASSERT_EQ(triangles.size(), 4u);
    EXPECT_TRUE(std::ranges::all_of(triangles, [](const auto& triangle) {
        return triangle.state == nw::nav::NavDebugPolygonState::walkable;
    }));

    constexpr std::array overlaps{
        nw::nav::NavBlockerOverlapInput{10, 2},
    };
    ASSERT_EQ(nw::nav::configure_nav_blockers(world, overlaps).overlap_count, 1u);
    const auto blocked = nw::nav::collect_nav_debug_triangles(world, triangles);
    EXPECT_EQ(blocked.output_count, 4u);
    EXPECT_EQ(std::ranges::count_if(triangles, [](const auto& triangle) {
        return triangle.state == nw::nav::NavDebugPolygonState::blocked;
    }),
        1);

    nw::nav::NavWorldState invalid;
    EXPECT_EQ(nw::nav::collect_nav_debug_triangles(invalid, triangles).rejected_count, 1u);
    EXPECT_TRUE(triangles.empty());
}

TEST(NavWorld, ProjectsRayToNearestWalkableSurface)
{
    auto geometry = make_stacked_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr std::array rays{
        nw::nav::NavRayProjectionInput{{5.0f, 5.0f, 10.0f}, {0.0f, 0.0f, -20.0f}},
    };
    std::array<nw::nav::NavRayProjectionResult, 1> results{};
    const auto stats = nw::nav::project_nav_rays(world, rays, results);

    EXPECT_EQ(stats.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    EXPECT_NEAR(results[0].position.x, 5.0f, 0.001f);
    EXPECT_NEAR(results[0].position.y, 5.0f, 0.001f);
    EXPECT_NEAR(results[0].position.z, 3.0f, 0.001f);
}

TEST(NavWorld, RayProjectionIgnoresBlockedPolygons)
{
    auto geometry = make_stacked_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 1, 1, world, build_stats),
        nw::nav::NavStatus::ok);
    constexpr std::array overlaps{
        nw::nav::NavBlockerOverlapInput{10, 2},
        nw::nav::NavBlockerOverlapInput{10, 3},
    };
    ASSERT_EQ(nw::nav::configure_nav_blockers(world, overlaps).overlap_count, 2u);

    constexpr std::array rays{
        nw::nav::NavRayProjectionInput{{5.0f, 5.0f, 10.0f}, {0.0f, 0.0f, -20.0f}},
    };
    std::array<nw::nav::NavRayProjectionResult, 1> results{};
    nw::nav::project_nav_rays(world, rays, results);

    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    EXPECT_NEAR(results[0].position.z, 0.0f, 0.001f);
}

TEST(NavWorld, RayProjectionRejectsInvalidRowsAndReportsMisses)
{
    auto geometry = make_two_tile_floor();
    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::nav::NavWorldState world;
    nw::nav::NavBuildStats build_stats;
    ASSERT_EQ(nw::nav::build_nav_world(geometry, walkable, 2, 1, world, build_stats),
        nw::nav::NavStatus::ok);

    constexpr std::array rays{
        nw::nav::NavRayProjectionInput{{5.0f, 5.0f, 10.0f}, {}},
        nw::nav::NavRayProjectionInput{{30.0f, 5.0f, 10.0f}, {0.0f, 0.0f, -20.0f}},
    };
    std::array<nw::nav::NavRayProjectionResult, 2> results{};
    const auto stats = nw::nav::project_nav_rays(world, rays, results);

    EXPECT_EQ(stats.rejected_count, 1u);
    EXPECT_EQ(stats.blocked_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::rejected);
    EXPECT_EQ(results[1].status, nw::nav::NavStatus::off_mesh);

    std::array<nw::nav::NavRayProjectionResult, 1> short_output{};
    const auto short_stats = nw::nav::project_nav_rays(world, rays, short_output);
    EXPECT_EQ(short_stats.rejected_count, rays.size());
}
