#include <gtest/gtest.h>

#include <nw/nav/NavBlockers.hpp>

#include <array>

TEST(NavBlockers, PreservesVerticalWallProjection)
{
    nw::nav::NavGeometry base;
    base.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
    };
    base.indices = {0, 2, 1, 1, 2, 3};
    base.surface = {1, 1};
    base.kind.assign(2, nw::nav::NavGeometryKind::tile_wok);
    base.owner = {0, 0};

    nw::nav::NavGeometry blockers;
    blockers.vertices = {
        {5.0f, 0.0f, 0.0f},
        {5.0f, 10.0f, 0.0f},
        {5.0f, 10.0f, 2.0f},
    };
    blockers.indices = {0, 1, 2};
    blockers.surface = {7};
    blockers.kind = {nw::nav::NavGeometryKind::door_dwk};
    blockers.owner = {42};

    constexpr std::array<uint8_t, 2> walkable{0, 1};
    nw::Vector<nw::nav::NavBlockerOverlapInput> overlaps;
    nw::nav::NavBlockerOverlapStats stats;
    ASSERT_EQ(nw::nav::build_nav_blocker_overlaps(
                  base, walkable, blockers, 1, 1, overlaps, stats),
        nw::nav::NavStatus::ok);

    EXPECT_EQ(stats.overlap_count, 2);
    ASSERT_EQ(overlaps.size(), 2);
    EXPECT_EQ(overlaps[0].owner, 42);
    EXPECT_EQ(overlaps[0].triangle, 0);
    EXPECT_EQ(overlaps[1].triangle, 1);
}

TEST(NavBlockers, RejectsNonWalkableAndSeparatedGeometry)
{
    nw::nav::NavGeometry base;
    base.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
    };
    base.indices = {0, 2, 1, 1, 2, 3};
    base.surface = {1, 2};
    base.kind.assign(2, nw::nav::NavGeometryKind::tile_wok);
    base.owner = {0, 0};

    nw::nav::NavGeometry blockers;
    blockers.vertices = {
        {1.0f, 1.0f, 5.0f},
        {2.0f, 1.0f, 5.0f},
        {1.0f, 2.0f, 5.0f},
    };
    blockers.indices = {0, 1, 2};
    blockers.surface = {7};
    blockers.kind = {nw::nav::NavGeometryKind::placeable_pwk};
    blockers.owner = {9};

    constexpr std::array<uint8_t, 3> walkable{0, 1, 0};
    nw::Vector<nw::nav::NavBlockerOverlapInput> overlaps;
    nw::nav::NavBlockerOverlapStats stats;
    ASSERT_EQ(nw::nav::build_nav_blocker_overlaps(
                  base, walkable, blockers, 1, 1, overlaps, stats),
        nw::nav::NavStatus::ok);

    EXPECT_TRUE(overlaps.empty());
    EXPECT_EQ(stats.rejected_base_triangle_count, 1);
}
