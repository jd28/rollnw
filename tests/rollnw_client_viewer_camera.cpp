#include <gtest/gtest.h>

#include "../tools/client/viewer_camera_state.hpp"

TEST(ClientViewerCameraStates, StoresDistinctSceneCamerasAndOverwritesMatchingRows)
{
    ClientViewerCameraStates states;
    nw::render::viewer::Camera area_camera;
    area_camera.set_free_view(
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
    area_camera.set_fov(71.0f);
    area_camera.set_near_far(0.25f, 800.0f);

    const auto invalid_kind = static_cast<ClientViewerSceneKind>(255);
    EXPECT_FALSE(states.store(ClientViewerSceneKind::area, {}, area_camera));
    EXPECT_FALSE(states.store(invalid_kind, "area_a", area_camera));
    EXPECT_FALSE(states.restore(invalid_kind, "area_a", area_camera));
    EXPECT_TRUE(states.store(ClientViewerSceneKind::area, "area_a", area_camera));
    EXPECT_EQ(states.size(), 1u);

    nw::render::viewer::Camera restored;
    EXPECT_FALSE(states.restore(ClientViewerSceneKind::area, "area_b", restored));
    ASSERT_TRUE(states.restore(ClientViewerSceneKind::area, "area_a", restored));
    EXPECT_EQ(restored.get_position(), area_camera.get_position());
    EXPECT_EQ(restored.get_target(), area_camera.get_target());
    EXPECT_EQ(restored.fov_degrees(), area_camera.fov_degrees());
    EXPECT_EQ(restored.near_plane(), area_camera.near_plane());
    EXPECT_EQ(restored.far_plane(), area_camera.far_plane());

    nw::render::viewer::Camera replacement;
    replacement.set_orbit_view(
        {7.0f, 8.0f, 9.0f}, 12.0f, 30.0f, 40.0f);
    EXPECT_TRUE(states.store(ClientViewerSceneKind::area, "area_a", replacement));
    EXPECT_EQ(states.size(), 1u);
    ASSERT_TRUE(states.restore(ClientViewerSceneKind::area, "area_a", restored));
    EXPECT_EQ(restored.get_view_matrix(), replacement.get_view_matrix());

    EXPECT_TRUE(states.store(ClientViewerSceneKind::preview, "area_a", area_camera));
    EXPECT_EQ(states.size(), 2u);
    ASSERT_TRUE(states.restore(ClientViewerSceneKind::preview, "area_a", restored));
    EXPECT_EQ(restored.get_view_matrix(), area_camera.get_view_matrix());

    states.clear();
    EXPECT_EQ(states.size(), 0u);
    EXPECT_FALSE(states.restore(ClientViewerSceneKind::area, "area_a", restored));
}
