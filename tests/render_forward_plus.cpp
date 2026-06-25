#include <gtest/gtest.h>

#include <nw/render/viewer/forward_plus.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

namespace {

nw::render::RenderContext make_forward_plus_test_context()
{
    nw::render::RenderContext ctx{};
    ctx.view = glm::mat4{1.0f};
    ctx.projection = glm::orthoRH_ZO(-8.0f, 8.0f, -8.0f, 8.0f, 0.1f, 64.0f);
    ctx.camera_position = glm::vec3{0.0f, 0.0f, 0.0f};
    ctx.camera_target = glm::vec3{0.0f, 0.0f, -1.0f};
    ctx.camera_near_plane = 0.1f;
    ctx.camera_far_plane = 64.0f;
    ctx.orthographic_camera = true;
    return ctx;
}

nw::render::LocalLight make_test_light(glm::vec3 position, float radius = 2.0f)
{
    return nw::render::LocalLight{
        .position = position,
        .radius = radius,
        .color = glm::vec3{1.0f, 0.8f, 0.5f},
        .intensity = 1.0f,
    };
}

nw::render::viewer::ForwardPlusConfig make_test_config(uint32_t max_lights_per_cluster = 8)
{
    return nw::render::viewer::ForwardPlusConfig{
        .tile_size = 64,
        .depth_slices = 4,
        .max_lights_per_cluster = max_lights_per_cluster,
    };
}

nw::render::RenderContext make_perspective_forward_plus_test_context()
{
    nw::render::RenderContext ctx{};
    ctx.view = glm::mat4{1.0f};
    ctx.projection = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.1f, 64.0f);
    ctx.camera_position = glm::vec3{0.0f, 0.0f, 0.0f};
    ctx.camera_target = glm::vec3{0.0f, 0.0f, -1.0f};
    ctx.camera_near_plane = 0.1f;
    ctx.camera_far_plane = 64.0f;
    return ctx;
}

bool tile_has_light(const nw::render::viewer::ForwardPlusFrame& frame, uint32_t tile_x, uint32_t tile_y)
{
    const auto dims = frame.resources.cluster_dimensions;
    if (tile_x >= dims.x || tile_y >= dims.y) {
        return false;
    }
    for (uint32_t z = 0; z < dims.z; ++z) {
        const uint32_t cluster = (z * dims.y + tile_y) * dims.x + tile_x;
        if (cluster < frame.cluster_headers.size() && frame.cluster_headers[cluster].count > 0u) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(RenderForwardPlus, EmptyLightSetBuildsDisabledFrame)
{
    auto ctx = make_forward_plus_test_context();
    nw::render::viewer::ForwardPlusFrame frame;

    nw::render::viewer::prepare_forward_plus_frame(
        frame, ctx, 0, 0, 128, 128, std::nullopt, make_test_config());

    EXPECT_FALSE(frame.resources.enabled);
    EXPECT_EQ(frame.resources.stats.light_count, 0u);
    EXPECT_EQ(frame.resources.cluster_dimensions.x, 2u);
    EXPECT_EQ(frame.resources.cluster_dimensions.y, 2u);
    EXPECT_EQ(frame.resources.cluster_dimensions.z, 4u);
    EXPECT_EQ(frame.resources.stats.cluster_count, 16u);
    EXPECT_TRUE(frame.cluster_light_indices.empty());
}

TEST(RenderForwardPlus, PreviewConfigUsesCoarseClusters)
{
    const auto config = nw::render::viewer::preview_forward_plus_config();

    EXPECT_EQ(config.tile_size, 64u);
    EXPECT_EQ(config.depth_slices, 8u);
    EXPECT_EQ(config.max_lights_per_cluster, 128u);
}

TEST(RenderForwardPlus, DefaultRenderPolicyMatchesPreviewConfig)
{
    const nw::render::viewer::ForwardPlusRenderPolicy policy{};
    const auto preview_config = nw::render::viewer::preview_forward_plus_config();

    EXPECT_TRUE(policy.enabled);
    EXPECT_TRUE(policy.auto_configure_area);
    EXPECT_TRUE(policy.gpu_culling);
    EXPECT_EQ(policy.debug_mode, nw::render::ForwardPlusDebugMode::off);
    EXPECT_EQ(policy.config.tile_size, preview_config.tile_size);
    EXPECT_EQ(policy.config.depth_slices, preview_config.depth_slices);
    EXPECT_EQ(policy.config.max_lights_per_cluster, preview_config.max_lights_per_cluster);
}

TEST(RenderForwardPlus, AreaConfigScalesWithVisibleSceneCost)
{
    const auto small = nw::render::viewer::area_forward_plus_config(128u, 128u);
    const auto medium = nw::render::viewer::area_forward_plus_config(512u, 768u);
    const auto large = nw::render::viewer::area_forward_plus_config(513u, 768u);

    EXPECT_EQ(small.tile_size, 64u);
    EXPECT_EQ(small.depth_slices, 4u);
    EXPECT_EQ(medium.tile_size, 64u);
    EXPECT_EQ(medium.depth_slices, 8u);
    EXPECT_EQ(large.tile_size, 32u);
    EXPECT_EQ(large.depth_slices, 16u);
}

TEST(RenderForwardPlus, FiltersExplicitLightIndexSet)
{
    auto ctx = make_forward_plus_test_context();
    ctx.local_lights = std::span<const nw::render::LocalLight>{};
    const std::vector<nw::render::LocalLight> lights{
        make_test_light({-1.0f, 0.0f, -4.0f}),
        make_test_light({0.0f, 0.0f, 5.0f}),
        make_test_light({1.0f, 0.0f, -4.0f}),
    };
    ctx.local_lights = lights;
    const uint32_t visible_light_indices[] = {0u, 2u, 99u};
    nw::render::viewer::ForwardPlusFrame frame;

    nw::render::viewer::prepare_forward_plus_frame(
        frame,
        ctx,
        0,
        0,
        128,
        128,
        std::span<const uint32_t>{visible_light_indices},
        make_test_config());

    ASSERT_TRUE(frame.resources.enabled);
    EXPECT_EQ(frame.lights.size(), 2u);
    EXPECT_EQ(frame.resources.stats.light_count, 2u);
    EXPECT_GT(frame.resources.stats.active_cluster_count, 0u);
    EXPECT_TRUE(std::all_of(frame.cluster_light_indices.begin(), frame.cluster_light_indices.end(),
        [](uint32_t light_index) { return light_index < 2u; }));
}

TEST(RenderForwardPlus, TracksClusterOverflow)
{
    auto ctx = make_forward_plus_test_context();
    const std::vector<nw::render::LocalLight> lights{
        make_test_light({0.0f, 0.0f, -4.0f}, 3.0f),
        make_test_light({0.25f, 0.0f, -4.0f}, 3.0f),
        make_test_light({-0.25f, 0.0f, -4.0f}, 3.0f),
    };
    ctx.local_lights = lights;
    nw::render::viewer::ForwardPlusFrame frame;

    nw::render::viewer::prepare_forward_plus_frame(
        frame, ctx, 0, 0, 128, 128, std::nullopt, make_test_config(1));

    ASSERT_TRUE(frame.resources.enabled);
    EXPECT_EQ(frame.resources.stats.light_count, 3u);
    EXPECT_GT(frame.resources.stats.max_lights_per_cluster, 1u);
    EXPECT_GT(frame.resources.stats.overflow_cluster_count, 0u);
    EXPECT_GT(frame.resources.stats.overflow_light_count, 0u);
}

TEST(RenderForwardPlus, PrioritizesLightsBeforeClusterCap)
{
    auto ctx = make_forward_plus_test_context();
    std::vector<nw::render::LocalLight> lights{
        make_test_light({0.0f, 0.0f, -4.0f}, 3.0f),
        make_test_light({0.0f, 0.0f, -4.0f}, 3.0f),
        make_test_light({0.0f, 0.0f, -4.0f}, 3.0f),
    };
    lights[0].intensity = 1.0f;
    lights[1].intensity = 5.0f;
    lights[2].intensity = 3.0f;
    ctx.local_lights = lights;
    nw::render::viewer::ForwardPlusFrame frame;

    nw::render::viewer::prepare_forward_plus_frame(
        frame, ctx, 0, 0, 128, 128, std::nullopt, make_test_config(1));

    ASSERT_TRUE(frame.resources.enabled);
    ASSERT_GE(frame.cluster_light_indices.size(), 3u);
    const auto cluster = std::find_if(frame.cluster_headers.begin(), frame.cluster_headers.end(),
        [](const auto& header) { return header.count >= 3u; });
    ASSERT_NE(cluster, frame.cluster_headers.end());

    const uint32_t first_light_index = frame.cluster_light_indices[cluster->offset];
    ASSERT_LT(first_light_index, frame.lights.size());
    EXPECT_FLOAT_EQ(frame.lights[first_light_index].color_intensity.w, 5.0f);
    EXPECT_GT(frame.resources.stats.overflow_cluster_count, 0u);
}

TEST(RenderForwardPlus, DropsInvalidAndOutOfFrustumLights)
{
    auto ctx = make_forward_plus_test_context();
    const std::vector<nw::render::LocalLight> lights{
        make_test_light({0.0f, 0.0f, -4.0f}),
        make_test_light({0.0f, 0.0f, 4.0f}),
        make_test_light({0.0f, 0.0f, -4.0f}, 0.0f),
    };
    ctx.local_lights = lights;
    nw::render::viewer::ForwardPlusFrame frame;

    nw::render::viewer::prepare_forward_plus_frame(
        frame, ctx, 0, 0, 128, 128, std::nullopt, make_test_config());

    ASSERT_TRUE(frame.resources.enabled);
    EXPECT_EQ(frame.lights.size(), 1u);
    EXPECT_EQ(frame.resources.stats.light_count, 1u);
    EXPECT_GT(frame.resources.stats.cluster_light_index_count, 0u);
}

TEST(RenderForwardPlus, PerspectiveLightsUseConservativeScreenBounds)
{
    auto ctx = make_perspective_forward_plus_test_context();
    const std::vector<nw::render::LocalLight> lights{
        make_test_light({0.0f, 0.0f, -3.0f}, 2.0f),
    };
    ctx.local_lights = lights;
    nw::render::viewer::ForwardPlusFrame frame;

    nw::render::viewer::prepare_forward_plus_frame(
        frame,
        ctx,
        0,
        0,
        128,
        128,
        std::nullopt,
        nw::render::viewer::ForwardPlusConfig{
            .tile_size = 16,
            .depth_slices = 4,
            .max_lights_per_cluster = 8,
        });

    ASSERT_TRUE(frame.resources.enabled);
    EXPECT_EQ(frame.resources.cluster_dimensions.x, 8u);
    EXPECT_EQ(frame.resources.cluster_dimensions.y, 8u);
    EXPECT_TRUE(tile_has_light(frame, 0u, 0u));
    EXPECT_TRUE(tile_has_light(frame, 7u, 7u));
}

TEST(RenderForwardPlus, SceneConstantsPackForwardPlusContract)
{
    nw::gfx::Pool<nw::gfx::Buffer, uint32_t> handles;
    const auto buffer = handles.insert(1u);
    nw::render::ForwardPlusResources resources{};
    resources.enabled = true;
    resources.tile_size = 32u;
    resources.max_lights_per_cluster = 64u;
    resources.cluster_dimensions = glm::uvec4{40u, 23u, 8u, 0u};
    resources.depth_params = glm::vec4{0.1f, 512.0f, 8.54091f, 0.0f};
    resources.viewport = glm::vec4{7.0f, 11.0f, 1280.0f, 720.0f};
    resources.light_buffer = nw::gfx::StorageSpan{buffer, 0u, 128u};
    resources.cluster_header_buffer = nw::gfx::StorageSpan{buffer, 128u, 256u};
    resources.cluster_light_index_buffer = nw::gfx::StorageSpan{buffer, 384u, 512u};
    resources.stats.light_count = 27u;

    nw::render::SceneConstants constants{};
    nw::render::apply_forward_plus_scene_constants(
        constants, resources, nw::render::ForwardPlusDebugMode::depth_slice);

    EXPECT_TRUE(nw::render::forward_plus_resources_ready(resources));
    EXPECT_EQ(constants.forward_plus_cluster_dims, (glm::uvec4{40u, 23u, 8u, 32u}));
    EXPECT_EQ(constants.forward_plus_params, (glm::uvec4{1u, 64u, 27u, 2u}));
    EXPECT_EQ(constants.forward_plus_depth_params, resources.depth_params);
    EXPECT_EQ(constants.forward_plus_viewport, resources.viewport);

    resources.cluster_light_index_buffer = {};
    nw::render::apply_forward_plus_scene_constants(
        constants, resources, nw::render::ForwardPlusDebugMode::cluster_light_count);

    EXPECT_FALSE(nw::render::forward_plus_resources_ready(resources));
    EXPECT_EQ(constants.forward_plus_params, (glm::uvec4{0u, 64u, 27u, 1u}));
}

TEST(RenderForwardPlus, ClusterIndexMatchesShaderLayout)
{
    nw::render::ForwardPlusResources resources{};
    resources.tile_size = 16u;
    resources.cluster_dimensions = glm::uvec4{4u, 3u, 5u, 0u};
    resources.depth_params = glm::vec4{1.0f, 101.0f, 0.0f, 1.0f};
    resources.viewport = glm::vec4{10.0f, 20.0f, 64.0f, 48.0f};

    EXPECT_EQ(nw::render::forward_plus_depth_slice_for_view_depth(resources, 41.0f), 2u);
    EXPECT_EQ(nw::render::forward_plus_cluster_index_for_screen_position(
                  resources, glm::vec2{43.0f, 67.0f}, 41.0f),
        34u);
    EXPECT_EQ(nw::render::forward_plus_cluster_index_for_screen_position(
                  resources, glm::vec2{-100.0f, -100.0f}, 1.0f),
        0u);
    EXPECT_EQ(nw::render::forward_plus_cluster_index_for_screen_position(
                  resources, glm::vec2{200.0f, 200.0f}, 200.0f),
        59u);
}
