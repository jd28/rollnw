#include <gtest/gtest.h>

#include "../tools/client/viewer_camera_state.hpp"
#include "../tools/client/viewport_pointer_drag.hpp"

#include <array>
#include <limits>

TEST(ClientViewportPointerDrag, ClickAndJitterRemainPendingUntilIntentionalMovement)
{
    RecordProperty("pointer_drag_bytes", sizeof(ClientViewportPointerDrag));
    const ClientViewportRect viewport{10, 20, 800, 600};
    ClientViewportPointerDrag drag{viewport, {300.0f, 200.0f}};
    for (const auto point : {glm::vec2{300.0f, 200.0f}, glm::vec2{302.0f, 199.0f},
             glm::vec2{304.0f, 204.0f}, glm::vec2{300.0f, 200.0f}}) {
        EXPECT_EQ(update_viewport_pointer_drag(drag, point, viewport), ClientViewportPointerDragStatus::pending);
        EXPECT_FALSE(drag.dragging);
    }
    EXPECT_EQ(update_viewport_pointer_drag(drag, {305.0f, 200.0f}, viewport), ClientViewportPointerDragStatus::dragging);
    EXPECT_TRUE(drag.dragging);
    EXPECT_EQ(update_viewport_pointer_drag(drag, {301.0f, 200.0f}, viewport), ClientViewportPointerDragStatus::dragging);
    EXPECT_EQ(update_viewport_pointer_drag(drag, drag.press_position, viewport), ClientViewportPointerDragStatus::dragging);
}

TEST(ClientViewportPointerDrag, ChangedViewportCancelsEvenWithoutPointerMotion)
{
    const ClientViewportRect original{10, 20, 800, 600};
    const std::array viewports{ClientViewportRect{10, 20, 680, 600},
        ClientViewportRect{11, 20, 800, 600}, ClientViewportRect{10, 21, 800, 600},
        ClientViewportRect{10, 20, 800, 601}, ClientViewportRect{}};
    for (const bool started : {false, true}) {
        for (const auto viewport : viewports) {
            ClientViewportPointerDrag drag{original, {300.0f, 200.0f}, started};
            EXPECT_EQ(update_viewport_pointer_drag(drag, drag.press_position, viewport),
                ClientViewportPointerDragStatus::cancelled);
        }
    }
}

TEST(ClientViewportPointerDrag, InvalidOrOutsidePointerSamplesCancel)
{
    const ClientViewportRect viewport{10, 20, 800, 600};
    for (const auto point : {glm::vec2{9.0f, 200.0f}, glm::vec2{810.0f, 200.0f},
             glm::vec2{300.0f, 19.0f}, glm::vec2{300.0f, 620.0f},
             glm::vec2{std::numeric_limits<float>::quiet_NaN(), 200.0f},
             glm::vec2{300.0f, std::numeric_limits<float>::infinity()}}) {
        ClientViewportPointerDrag drag{viewport, {300.0f, 200.0f}};
        EXPECT_EQ(update_viewport_pointer_drag(drag, point, viewport), ClientViewportPointerDragStatus::cancelled);
        EXPECT_FALSE(drag.dragging);
        ClientViewportPointerDrag invalid_press{viewport, point};
        EXPECT_EQ(update_viewport_pointer_drag(invalid_press, {300.0f, 200.0f}, viewport),
            ClientViewportPointerDragStatus::cancelled);
    }
}

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
