#include <gtest/gtest.h>

#include <nw/render/viewer/area_lighting.hpp>
#include <nw/render/viewer/area_render_scene.hpp>
#include <nw/render/viewer/camera.hpp>
#include <nw/render/viewer/preview_model_draws.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/scene_lights.hpp>
#include <nw/render/viewer/scene_particles.hpp>
#include <nw/render/viewer/scene_shadow.hpp>
#include <nw/render/viewer/tile_light.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace {

std::unique_ptr<nw::render::RenderModel> make_area_mesh_model(
    nw::render::MaterialMode material_mode = nw::render::MaterialMode::opaque);

void add_grid_area_tiles(nw::render::viewer::PreviewScene& scene, int width, int height)
{
    namespace viewer = nw::render::viewer;

    scene.is_area = true;
    scene.area_width = static_cast<uint32_t>(std::max(width, 0));
    scene.area_height = static_cast<uint32_t>(std::max(height, 0));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto tile = make_area_mesh_model();
            const float min_x = static_cast<float>(x) * viewer::kAreaRenderTileSize;
            const float min_y = static_cast<float>(y) * viewer::kAreaRenderTileSize;
            tile->bounds = nw::render::Bounds{
                .min = {min_x, min_y, 0.0f},
                .max = {min_x + viewer::kAreaRenderTileSize, min_y + viewer::kAreaRenderTileSize, 1.0f},
            };
            tile->primitives.front().bounds = tile->bounds;
            scene.add(std::move(tile));
            scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
                .kind = viewer::AreaRenderRecordKind::tile,
                .tile_x = static_cast<int16_t>(x),
                .tile_y = static_cast<int16_t>(y),
                .static_candidate = true,
            };
        }
    }
}

size_t chunk_index(uint32_t width, uint32_t x, uint32_t y)
{
    return static_cast<size_t>(y) * width + x;
}

nw::gfx::Handle<nw::gfx::Buffer> dummy_buffer_handle()
{
    static nw::gfx::Pool<nw::gfx::Buffer, int> buffers;
    return buffers.insert(1);
}

void expect_prepared_surface_stream_maps_to_draws(
    const nw::render::viewer::AreaRenderScene& area_scene)
{
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto& draws = area_scene.prepared_model_draw_list().draws;
    const auto surface_indices = area_scene.prepared_surface_indices();

    for (size_t i = 0; i < surface_indices.size(); ++i) {
        const uint32_t surface_index = surface_indices[i];
        ASSERT_LT(surface_index, surfaces.size());
        const auto& surface = surfaces[surface_index];
        ASSERT_LT(surface.draw_index, draws.size());
        EXPECT_EQ(draws[surface.draw_index].material_mode, surface.material_mode);
    }
}

std::unique_ptr<nw::render::RenderModel> make_area_mesh_model(
    nw::render::MaterialMode material_mode)
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {2.0f, 3.0f, 4.0f},
    };
    model->materials.push_back(nw::render::Material{
        .alpha_mode = material_mode,
    });
    model->primitives.push_back(nw::render::Primitive{
        .vertices = dummy_buffer_handle(),
        .indices = dummy_buffer_handle(),
        .vertex_count = 3,
        .index_count = 3,
        .material = 0,
        .casts_shadow = false,
        .bounds = model->bounds,
    });

    return model;
}

void add_static_area_mesh_model(
    nw::render::viewer::PreviewScene& scene,
    nw::render::MaterialMode material_mode,
    uint32_t tile_x,
    bool render_enabled = true)
{
    namespace viewer = nw::render::viewer;

    auto model = make_area_mesh_model(material_mode);
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(scene.static_models.size() - 1u);
    ASSERT_NE(instance, nullptr);
    instance->visible = render_enabled;
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = static_cast<int16_t>(
            std::min<uint32_t>(tile_x, static_cast<uint32_t>(std::numeric_limits<int16_t>::max()))),
        .tile_y = 0,
        .static_candidate = true,
    };
}

void add_static_transparent_area_mesh_model(nw::render::viewer::PreviewScene& scene, uint32_t tile_x)
{
    add_static_area_mesh_model(scene, nw::render::MaterialMode::transparent, tile_x);
}

std::unique_ptr<nw::render::RenderModel> make_shadow_render_model(
    nw::render::MaterialMode material_mode = nw::render::MaterialMode::opaque)
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->materials.push_back(nw::render::Material{
        .alpha_mode = material_mode,
    });
    model->primitives.push_back(nw::render::Primitive{
        .vertices = {},
        .indices = {},
        .vertex_count = 3,
        .index_count = 3,
        .material = 0,
        .bounds = {
            .min = {0.0f, 0.0f, 0.0f},
            .max = {2.0f, 3.0f, 4.0f},
        },
    });
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {2.0f, 3.0f, 4.0f},
    };
    return model;
}

} // namespace

TEST(RenderViewerLighting, DayNightTransitionRetunesAreaLocalLights)
{
    nw::render::viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_weather.day_night_cycle = 1;
    scene.area_weather.is_night = 0;
    scene.local_lights.push_back(nw::render::viewer::SceneLocalLight{
        .position = {1.0f, 2.0f, 3.0f},
        .radius = 6.0f,
        .color = {1.0f, 0.8f, 0.5f},
        .intensity = 0.86f,
        .base_radius = 6.0f,
        .base_intensity = 0.86f,
        .source = nw::render::viewer::SceneLocalLightSource::tile_model,
    });

    nw::render::viewer::refresh_scene_local_light_render_data(scene);
    ASSERT_EQ(scene.render_local_lights.size(), 1u);
    EXPECT_NEAR(scene.local_lights[0].radius, 6.0f * 0.62f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].intensity, 0.86f * 0.28f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].radius, scene.local_lights[0].radius, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].intensity, scene.local_lights[0].intensity, 1.0e-5f);

    const bool changed = nw::render::viewer::sync_area_day_night_state(
        scene, nw::render::viewer::kAreaDayNightCycleSeconds * 0.75f, false);

    ASSERT_TRUE(changed);
    ASSERT_EQ(scene.render_local_lights.size(), 1u);
    EXPECT_EQ(scene.area_weather.is_night, 1u);
    EXPECT_NEAR(scene.local_lights[0].radius, 6.0f * 0.80f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].intensity, 0.86f * 0.50f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].radius, scene.local_lights[0].radius, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].intensity, scene.local_lights[0].intensity, 1.0e-5f);
}

TEST(RenderViewerLighting, ModernAreaModelsUseAuthoredCelestialLighting)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_weather.day_night_cycle = 1;
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 0u);

    const auto day = viewer::resolve_preview_scene_lighting(
        scene, viewer::kAreaDayNightCycleSeconds * 0.25f, viewer::AreaLightingMode::authored);
    const auto night = viewer::resolve_preview_scene_lighting(
        scene, viewer::kAreaDayNightCycleSeconds * 0.75f, viewer::AreaLightingMode::authored);

    EXPECT_GT(day.key_intensity, 0.0f);
    EXPECT_GT(glm::length(day.ambient), 0.0f);
    EXPECT_GT(night.key_intensity, 0.0f);
    EXPECT_GT(glm::length(night.ambient), 0.0f);
    EXPECT_GT(glm::length(day.key_direction - night.key_direction), 0.1f);
}

// Cascade shadows hang off the resolved key light: resolve_scene_shadow bails
// when key_intensity is ~0. A lighting resolver that returns unlit for an area
// therefore silently disables the whole directional shadow pass, which is how
// this regression shipped unnoticed. Guard the link, not just the resolver.
TEST(RenderViewerLighting, AreaAuthoredLightingEnablesCascadeShadows)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_weather.day_night_cycle = 1;
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 0u);

    nw::render::RenderContext ctx{};
    ctx.camera_position = {0.0f, -20.0f, 15.0f};
    ctx.camera_target = {0.0f, 0.0f, 0.0f};
    ctx.camera_near_plane = 0.1f;
    ctx.camera_far_plane = 200.0f;
    ctx.view = glm::lookAt(ctx.camera_position, ctx.camera_target, glm::vec3{0.0f, 0.0f, 1.0f});
    ctx.projection = glm::perspective(glm::radians(60.0f), 1.0f, ctx.camera_near_plane, ctx.camera_far_plane);
    ctx.lighting = viewer::resolve_preview_scene_lighting(
        scene, viewer::kAreaDayNightCycleSeconds * 0.25f, viewer::AreaLightingMode::authored);
    ctx.lighting_space = viewer::resolve_preview_scene_lighting_space(
        scene, viewer::AreaLightingMode::authored);

    ASSERT_EQ(ctx.lighting_space, nw::render::LightingSpace::world_space);

    const auto shadow = viewer::resolve_scene_shadow(
        ctx, scene.bounds, viewer::kSceneShadowMapResolution, nw::render::kShadowCascadeCount);

    EXPECT_TRUE(shadow.enabled);
    EXPECT_EQ(shadow.cascade_count, nw::render::kShadowCascadeCount);
    EXPECT_GT(shadow.strength, 0.0f);
}

TEST(RenderViewerLighting, NwnRenderModelsUseStudioLighting)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = make_area_mesh_model();
    model->source_kind = nw::render::ModelAssetSourceKind::nwn;
    scene.add(std::move(model));

    const auto lighting = viewer::resolve_preview_scene_lighting(
        scene, 0.0f, viewer::AreaLightingMode::authored);

    EXPECT_GT(lighting.key_intensity, 0.0f);
    EXPECT_GT(glm::length(lighting.ambient), 0.0f);
}

TEST(RenderViewerLighting, GltfRenderModelsRemainUnlit)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = make_area_mesh_model();
    model->source_kind = nw::render::ModelAssetSourceKind::gltf;
    scene.add(std::move(model));

    const auto lighting = viewer::resolve_preview_scene_lighting(
        scene, 0.0f, viewer::AreaLightingMode::authored);

    EXPECT_FLOAT_EQ(lighting.key_intensity, 0.0f);
    EXPECT_FLOAT_EQ(glm::length(lighting.ambient), 0.0f);
}

TEST(RenderViewerLighting, DynamicLocalLightFollowsTrackedModelNode)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->nodes.push_back(nw::render::Node{
        .parent = -1,
        .world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}),
    });
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 30.0f});
    viewer::sync_model_instance_runtime_state(scene);

    scene.local_lights.push_back(viewer::SceneLocalLight{
        .position = {0.0f, 0.0f, 0.0f},
        .radius = 4.0f,
        .color = {1.0f, 0.8f, 0.5f},
        .intensity = 1.0f,
        .base_radius = 4.0f,
        .base_intensity = 1.0f,
        .source = viewer::SceneLocalLightSource::authored_model,
        .model_index = 0,
        .model_source_node_index = 0,
    });
    viewer::refresh_scene_local_light_render_data(scene);

    const bool changed = viewer::refresh_scene_dynamic_local_light_render_data(scene);

    ASSERT_TRUE(changed);
    ASSERT_EQ(scene.render_local_lights.size(), 1u);
    EXPECT_NEAR(scene.local_lights[0].position.x, 11.0f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].position.y, 22.0f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].position.z, 33.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.x, scene.local_lights[0].position.x, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.y, scene.local_lights[0].position.y, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.z, scene.local_lights[0].position.z, 1.0e-5f);
}

TEST(RenderViewerLighting, DynamicLocalLightUsesCommonInstanceRoot)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->nodes.push_back(nw::render::Node{
        .parent = -1,
        .world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}),
    });
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{100.0f, 200.0f, 300.0f});
    viewer::sync_model_instance_runtime_state(scene);

    scene.local_lights.push_back(viewer::SceneLocalLight{
        .position = {0.0f, 0.0f, 0.0f},
        .radius = 4.0f,
        .color = {1.0f, 0.8f, 0.5f},
        .intensity = 1.0f,
        .base_radius = 4.0f,
        .base_intensity = 1.0f,
        .source = viewer::SceneLocalLightSource::authored_model,
        .model_index = 0,
        .model_source_node_index = 0,
    });
    viewer::refresh_scene_local_light_render_data(scene);

    const bool changed = viewer::refresh_scene_dynamic_local_light_render_data(scene);

    ASSERT_TRUE(changed);
    ASSERT_EQ(scene.render_local_lights.size(), 1u);
    EXPECT_NEAR(scene.local_lights[0].position.x, 101.0f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].position.y, 202.0f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].position.z, 303.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.x, scene.local_lights[0].position.x, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.y, scene.local_lights[0].position.y, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.z, scene.local_lights[0].position.z, 1.0e-5f);
}

TEST(RenderViewerLighting, DynamicLocalLightRejectsStaleCommonInstance)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->nodes.push_back(nw::render::Node{
        .parent = -1,
        .world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}),
    });
    scene.add(std::move(model));
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    scene.local_lights.push_back(viewer::SceneLocalLight{
        .position = {0.0f, 0.0f, 0.0f},
        .radius = 4.0f,
        .color = {1.0f, 0.8f, 0.5f},
        .intensity = 1.0f,
        .base_radius = 4.0f,
        .base_intensity = 1.0f,
        .source = viewer::SceneLocalLightSource::authored_model,
        .model_index = 0,
        .model_source_node_index = 0,
    });
    viewer::refresh_scene_local_light_render_data(scene);
    scene.model_instances.destroy(scene.static_model_instance_handles[0]);

    const bool changed = viewer::refresh_scene_dynamic_local_light_render_data(scene);

    EXPECT_TRUE(changed);
    ASSERT_EQ(scene.render_local_lights.size(), 1u);
    EXPECT_NEAR(scene.local_lights[0].position.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.local_lights[0].position.z, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].position.z, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].radius, 0.0f, 1.0e-5f);
    EXPECT_NEAR(scene.render_local_lights[0].intensity, 0.0f, 1.0e-5f);
}

TEST(RenderViewerCamera, AreaGameplayViewUsesLowPerspectiveCamera)
{
    nw::render::viewer::Camera camera;
    camera.set_area_gameplay_view(nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {100.0f, 80.0f, 10.0f},
    });

    const glm::vec3 position = camera.get_position();
    const glm::vec3 target = camera.get_target();
    const glm::vec3 delta = target - position;

    EXPECT_FALSE(camera.is_orthographic());
    EXPECT_NEAR(camera.fov_degrees(), 65.0f, 1.0e-5f);
    EXPECT_NEAR(position.x, 18.0f, 1.0e-5f);
    EXPECT_NEAR(position.y, 14.4f, 1.0e-5f);
    EXPECT_NEAR(position.z, 7.0f, 1.0e-5f);
    EXPECT_GT(target.x, position.x);
    EXPECT_GT(target.y, position.y);
    EXPECT_LT(target.z, position.z);
    EXPECT_NEAR(glm::length(glm::vec2{delta.x, delta.y}), 35.0f, 1.0e-4f);
}

TEST(RenderViewerCamera, FocusOnFramesBoundsFromCurrentSide)
{
    nw::render::viewer::Camera camera;
    camera.set_area_overview(nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {100.0f, 80.0f, 10.0f},
    });

    const glm::vec3 overview_offset = camera.get_position() - camera.get_target();
    const nw::render::Bounds overview_bounds{
        .min = {23.0f, 28.0f, 2.0f},
        .max = {27.0f, 32.0f, 6.0f},
    };
    const glm::vec3 overview_target = overview_bounds.center();
    ASSERT_TRUE(camera.focus_on(overview_bounds));

    EXPECT_TRUE(camera.is_orthographic());
    EXPECT_EQ(camera.get_target(), overview_target);
    EXPECT_NEAR(glm::length(
                    (camera.get_position() - camera.get_target()) - overview_offset),
        0.0f,
        1.0e-5f);

    camera.set_area_gameplay_view(nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {100.0f, 80.0f, 10.0f},
    });
    camera.move_forward(4.0f);
    camera.yaw(15.0f);
    ASSERT_NEAR(glm::length(camera.get_position() - camera.get_target()), 1.0f, 1.0e-5f);

    const nw::render::Bounds gameplay_bounds{
        .min = {68.0f, 63.0f, 1.0f},
        .max = {72.0f, 67.0f, 5.0f},
    };
    const glm::vec3 gameplay_target = gameplay_bounds.center();
    const glm::vec2 gameplay_approach = glm::normalize(
        glm::vec2{camera.get_position() - gameplay_target});
    ASSERT_TRUE(camera.focus_on(gameplay_bounds));
    const glm::vec4 gameplay_clip = camera.get_projection_matrix()
        * camera.get_view_matrix()
        * glm::vec4{gameplay_target, 1.0f};

    EXPECT_EQ(camera.get_target(), gameplay_target);
    EXPECT_NEAR(
        glm::length(camera.get_position() - camera.get_target()),
        gameplay_bounds.radius() * 2.5f,
        1.0e-5f);
    EXPECT_NEAR(
        glm::dot(
            glm::normalize(glm::vec2{camera.get_position() - camera.get_target()}),
            gameplay_approach),
        1.0f,
        1.0e-5f);
    ASSERT_GT(std::abs(gameplay_clip.w), 1.0e-5f);
    EXPECT_NEAR(gameplay_clip.x / gameplay_clip.w, 0.0f, 1.0e-5f);
    EXPECT_NEAR(gameplay_clip.y / gameplay_clip.w, 0.0f, 1.0e-5f);

    camera.set_orbit_view({1.0f, 2.0f, 3.0f}, 12.0f, 35.0f, 20.0f);
    const nw::render::Bounds orbit_bounds{
        .min = {38.0f, 48.0f, 4.0f},
        .max = {42.0f, 52.0f, 8.0f},
    };
    const glm::vec3 orbit_target = orbit_bounds.center();
    const glm::vec2 orbit_approach = glm::normalize(
        glm::vec2{camera.get_position() - orbit_target});
    ASSERT_TRUE(camera.focus_on(orbit_bounds));

    EXPECT_FALSE(camera.is_orthographic());
    EXPECT_EQ(camera.get_target(), orbit_target);
    EXPECT_NEAR(
        glm::length(camera.get_position() - camera.get_target()),
        orbit_bounds.radius() * 2.5f,
        1.0e-5f);
    EXPECT_NEAR(
        glm::dot(
            glm::normalize(glm::vec2{camera.get_position() - camera.get_target()}),
            orbit_approach),
        1.0f,
        1.0e-5f);

    const glm::vec3 position_before_invalid_target = camera.get_position();
    EXPECT_FALSE(camera.focus_on(nw::render::Bounds{
        .min = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    }));
    EXPECT_EQ(camera.get_target(), orbit_target);
    EXPECT_EQ(camera.get_position(), position_before_invalid_target);
}

TEST(RenderViewerLighting, FilteredAreaFrameKeepsModelBoundDynamicLights)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    auto first = make_area_mesh_model();
    first->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    first->primitives.front().bounds = first->bounds;
    scene.add(std::move(first));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    auto second = make_area_mesh_model();
    second->bounds = nw::render::Bounds{.min = {10.0f, 0.0f, 0.0f}, .max = {11.0f, 1.0f, 1.0f}};
    second->primitives.front().bounds = second->bounds;
    scene.add(std::move(second));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 1,
        .tile_y = 0,
        .static_candidate = true,
    };

    scene.local_lights.push_back(viewer::SceneLocalLight{
        .position = {0.5f, 0.5f, 0.5f},
        .radius = 1.0f,
        .color = {1.0f, 1.0f, 1.0f},
        .intensity = 1.0f,
        .base_radius = 1.0f,
        .base_intensity = 1.0f,
        .source = viewer::SceneLocalLightSource::tile_model,
    });
    scene.local_lights.push_back(viewer::SceneLocalLight{
        .position = {0.5f, 0.5f, 0.5f},
        .radius = 1.0f,
        .color = {1.0f, 0.6f, 0.3f},
        .intensity = 1.0f,
        .base_radius = 1.0f,
        .base_intensity = 1.0f,
        .source = viewer::SceneLocalLightSource::authored_model,
        .model_index = 0,
        .model_source_node_index = 0,
    });
    viewer::refresh_scene_local_light_render_data(scene);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.dynamic_light_indices().size(), 1u);
    EXPECT_EQ(area_scene.dynamic_light_indices()[0], 1u);

    viewer::AreaRenderFrame frame;
    const uint8_t visible_chunks[] = {0u, 1u};
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = std::span<const uint8_t>{visible_chunks};
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.uses_filtered_light_indices());
    ASSERT_EQ(frame.visible_light_indices().size(), 1u);
    EXPECT_EQ(frame.visible_light_indices()[0], 1u);
}

TEST(RenderViewerLighting, LocalShadowSelectionIsStableAcrossCameraMotion)
{
    nw::render::RenderContext ctx{};
    std::vector<nw::render::LocalLight> lights{
        nw::render::LocalLight{.position = {100.0f, 0.0f, 4.0f}, .radius = 80.0f, .intensity = 1.0f},
        nw::render::LocalLight{.position = {150.0f, 0.0f, 4.0f}, .radius = 80.0f, .intensity = 1.0f},
        nw::render::LocalLight{.position = {200.0f, 0.0f, 4.0f}, .radius = 80.0f, .intensity = 1.0f},
        nw::render::LocalLight{.position = {250.0f, 0.0f, 4.0f}, .radius = 80.0f, .intensity = 1.0f},
        nw::render::LocalLight{.position = {400.0f, 0.0f, 4.0f}, .radius = 10.0f, .intensity = 1.0f},
    };

    ctx.camera_position = {0.0f, 0.0f, 4.0f};
    auto shadows = nw::render::viewer::resolve_local_shadows(ctx, lights);
    ASSERT_EQ(shadows.count, nw::render::kLocalShadowCount);
    EXPECT_EQ(lights[0].shadow_slot, 0);
    EXPECT_EQ(lights[1].shadow_slot, 1);
    EXPECT_EQ(lights[2].shadow_slot, 2);
    EXPECT_EQ(lights[3].shadow_slot, 3);
    EXPECT_EQ(lights[4].shadow_slot, -1);

    ctx.camera_position = lights[4].position;
    shadows = nw::render::viewer::resolve_local_shadows(ctx, lights);
    ASSERT_EQ(shadows.count, nw::render::kLocalShadowCount);
    EXPECT_EQ(lights[0].shadow_slot, 0);
    EXPECT_EQ(lights[1].shadow_slot, 1);
    EXPECT_EQ(lights[2].shadow_slot, 2);
    EXPECT_EQ(lights[3].shadow_slot, 3);
    EXPECT_EQ(lights[4].shadow_slot, -1);
}

TEST(RenderViewerShadow, PreparedSurfaceDropsWaterShadowCaster)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = make_area_mesh_model(nw::render::MaterialMode::water);
    ASSERT_FALSE(model->primitives.empty());
    model->primitives[0].casts_shadow = true;
    model->shadow = nw::render::summarize_render_model_shadows(*model);
    scene.add(std::move(model));

    const auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->shadow.casts_shadow);

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene);

    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(
        surfaces,
        prepared.common,
        prepared.ranges);

    EXPECT_TRUE(surfaces.stats.valid());
    EXPECT_EQ(surfaces.stats.draw_count, 1u);
    EXPECT_EQ(surfaces.stats.shadow_caster_draw_count, 0u);
    ASSERT_EQ(surfaces.draws.size(), 1u);
    EXPECT_EQ(surfaces.draws[0].material_mode, nw::render::MaterialMode::water);
    EXPECT_FALSE(surfaces.draws[0].casts_shadow);
}

TEST(RenderViewerShadow, PreparedSurfacesUseCommonInstanceShadowSummary)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_area_mesh_model(nw::render::MaterialMode::opaque));
    scene.add(make_shadow_render_model());
    auto* first_instance = scene.static_model_instance(0);
    ASSERT_NE(first_instance, nullptr);
    first_instance->shadow.casts_shadow = false;

    viewer::PreviewPreparedModelDraws prepared;
    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);

    ASSERT_EQ(surfaces.draws.size(), 2u);
    EXPECT_EQ(surfaces.stats.shadow_caster_draw_count, 1u);
    EXPECT_FALSE(surfaces.draws[0].casts_shadow);
    EXPECT_TRUE(surfaces.draws[1].casts_shadow);

    auto* second_instance = scene.static_model_instance(1);
    ASSERT_NE(second_instance, nullptr);
    second_instance->shadow.casts_shadow = false;
    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);

    EXPECT_EQ(surfaces.stats.shadow_caster_draw_count, 0u);
    ASSERT_EQ(surfaces.draws.size(), 2u);
    EXPECT_FALSE(surfaces.draws[0].casts_shadow);
    EXPECT_FALSE(surfaces.draws[1].casts_shadow);
}

TEST(RenderViewerShadow, RenderModelPrimitiveShadowFlagSuppressesPreparedSurface)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = make_shadow_render_model();
    ASSERT_FALSE(model->primitives.empty());
    model->primitives[0].casts_shadow = false;
    model->shadow = nw::render::summarize_render_model_shadows(*model);
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->shadow.casts_shadow = true;
    instance->shadow.caster_count = 1u;

    viewer::PreviewPreparedModelDraws prepared;
    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);

    ASSERT_EQ(prepared.common.draws.size(), 1u);
    EXPECT_TRUE(prepared.common.draws[0].instance_casts_shadow);
    EXPECT_FALSE(prepared.common.draws[0].primitive_casts_shadow);
    ASSERT_EQ(surfaces.draws.size(), 1u);
    EXPECT_FALSE(surfaces.draws[0].casts_shadow);
    EXPECT_EQ(surfaces.stats.shadow_caster_draw_count, 0u);
}

TEST(RenderViewerShadow, RenderModelRuntimeSyncTracksWorldBoundsAndShadowSummary)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_shadow_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->visible = false;
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 3.0f});

    viewer::sync_model_instance_runtime_state(scene);

    instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->visible);
    EXPECT_NEAR(instance->current_bounds.min.x, 10.0f, 1.0e-5f);
    EXPECT_NEAR(instance->current_bounds.min.y, 20.0f, 1.0e-5f);
    EXPECT_NEAR(instance->current_bounds.min.z, 3.0f, 1.0e-5f);
    EXPECT_NEAR(instance->current_bounds.max.x, 12.0f, 1.0e-5f);
    EXPECT_NEAR(instance->current_bounds.max.y, 23.0f, 1.0e-5f);
    EXPECT_NEAR(instance->current_bounds.max.z, 7.0f, 1.0e-5f);
    EXPECT_TRUE(instance->shadow.casts_shadow);
    EXPECT_EQ(instance->shadow.caster_count, 1u);
    EXPECT_NEAR(instance->shadow.bounds.min.x, instance->current_bounds.min.x, 1.0e-5f);
    EXPECT_NEAR(instance->shadow.bounds.max.z, instance->current_bounds.max.z, 1.0e-5f);

    const auto scene_bounds = scene.current_bounds();
    EXPECT_NEAR(scene_bounds.min.x, 10.0f, 1.0e-5f);
    EXPECT_NEAR(scene_bounds.max.z, 7.0f, 1.0e-5f);
}

TEST(RenderAreaVisibility, RadiusMaskKeepsSquareAroundCamera)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    add_grid_area_tiles(scene, 5, 5);
    for (const size_t model_index : {chunk_index(5u, 2u, 2u), chunk_index(5u, 4u, 4u)}) {
        auto& model = *scene.static_models[model_index];
        model.primitives.front().casts_shadow = true;
        model.shadow = nw::render::summarize_render_model_shadows(model);
    }
    viewer::sync_model_instance_runtime_state(scene);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.stats().chunk_width, 5u);
    ASSERT_EQ(area_scene.stats().chunk_height, 5u);
    const uint32_t center_record = area_scene.record_index_for_render_model(
        static_cast<uint32_t>(chunk_index(5u, 2u, 2u)));
    const uint32_t far_record = area_scene.record_index_for_render_model(
        static_cast<uint32_t>(chunk_index(5u, 4u, 4u)));
    ASSERT_NE(center_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(far_record, viewer::kInvalidAreaRenderRecordIndex);

    std::vector<uint8_t> mask;
    const size_t visible = viewer::rebuild_area_visibility_mask(
        area_scene,
        viewer::AreaVisibilityMaskOptions{
            .camera_position = {25.0f, 25.0f, 1.0f},
            .camera_target = {35.0f, 25.0f, 1.0f},
            .radius_tiles = 1,
            .mode = viewer::AreaVisibilityMaskMode::radius,
        },
        mask);

    ASSERT_EQ(mask.size(), 25u);
    EXPECT_EQ(visible, 9u);
    EXPECT_EQ(std::count(mask.begin(), mask.end(), uint8_t{1u}), 9);
    EXPECT_EQ(mask[chunk_index(5u, 2u, 2u)], 1u);
    EXPECT_EQ(mask[chunk_index(5u, 0u, 2u)], 0u);
    EXPECT_EQ(mask[chunk_index(5u, 4u, 4u)], 0u);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = mask;
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);
    EXPECT_TRUE(frame.record_visible(center_record));
    EXPECT_FALSE(frame.record_visible(far_record));
    ASSERT_TRUE(frame.has_visible_bounds());
    EXPECT_NEAR(frame.visible_bounds().min.x, 10.0f, 1.0e-5f);
    EXPECT_NEAR(frame.visible_bounds().min.y, 10.0f, 1.0e-5f);
    EXPECT_NEAR(frame.visible_bounds().max.x, 40.0f, 1.0e-5f);
    EXPECT_NEAR(frame.visible_bounds().max.y, 40.0f, 1.0e-5f);
    ASSERT_TRUE(frame.has_shadow_caster_bounds());
    EXPECT_NEAR(frame.shadow_caster_bounds().min.x, 20.0f, 1.0e-5f);
    EXPECT_NEAR(frame.shadow_caster_bounds().min.y, 20.0f, 1.0e-5f);
    EXPECT_NEAR(frame.shadow_caster_bounds().max.x, 30.0f, 1.0e-5f);
    EXPECT_NEAR(frame.shadow_caster_bounds().max.y, 30.0f, 1.0e-5f);
}

TEST(RenderAreaVisibility, DynamicRecordRefreshUsesCommonInstanceRuntimeState)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 5;
    scene.area_height = 1;

    auto model = make_shadow_render_model();
    model->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    model->primitives.front().bounds = model->bounds;
    scene.add(std::move(model));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.stats().dynamic_record_count, 1u);
    ASSERT_EQ(area_scene.model_instance_handles().size(), 1u);

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{20.0f, 2.0f, 0.0f});
    instance->current_bounds = nw::render::Bounds{
        .min = {20.0f, 2.0f, 0.0f},
        .max = {21.0f, 3.0f, 1.0f},
    };
    instance->shadow.bounds = instance->current_bounds;
    instance->shadow.casts_shadow = true;
    instance->shadow.caster_count = 1u;

    area_scene.refresh_runtime_records(scene);

    ASSERT_EQ(area_scene.bounds().size(), 1u);
    EXPECT_NEAR(area_scene.bounds()[0].min.x, 20.0f, 1.0e-5f);
    ASSERT_EQ(area_scene.root_transforms().size(), 1u);
    EXPECT_NEAR(area_scene.root_transforms()[0][3].x, 20.0f, 1.0e-5f);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    EXPECT_TRUE(frame.record_visible(0u));
    ASSERT_TRUE(frame.has_visible_bounds());
    EXPECT_NEAR(frame.visible_bounds().min.x, 20.0f, 1.0e-5f);
    ASSERT_TRUE(frame.has_shadow_caster_bounds());
    EXPECT_NEAR(frame.shadow_caster_bounds().max.x, 21.0f, 1.0e-5f);
}

TEST(RenderAreaVisibility, RenderModelAreaRecordUsesCommonInstanceState)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    scene.add(make_shadow_render_model());
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 1,
        .tile_y = 0,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    const uint32_t record = area_scene.record_index_for_render_model(0u);
    ASSERT_NE(record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_EQ(area_scene.model_indices().size(), 1u);
    EXPECT_EQ(area_scene.model_indices()[record], 0u);
    EXPECT_EQ(area_scene.stats().record_count, 1u);
    EXPECT_EQ(area_scene.stats().dynamic_record_count, 1u);
    EXPECT_EQ(area_scene.stats().creature_record_count, 1u);
    EXPECT_EQ(area_scene.stats().opaque_cutout_record_count, 1u);
    EXPECT_EQ(area_scene.stats().shadow_caster_record_count, 1u);

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    instance->current_bounds = nw::render::Bounds{
        .min = {10.0f, 0.0f, 0.0f},
        .max = {12.0f, 3.0f, 4.0f},
    };
    instance->shadow.bounds = instance->current_bounds;
    instance->shadow.casts_shadow = true;
    instance->shadow.caster_count = 1u;
    area_scene.refresh_runtime_records(scene);

    ASSERT_EQ(area_scene.bounds().size(), 1u);
    EXPECT_NEAR(area_scene.bounds()[record].min.x, 10.0f, 1.0e-5f);
    ASSERT_EQ(area_scene.root_transforms().size(), 1u);
    EXPECT_NEAR(area_scene.root_transforms()[record][3].x, 10.0f, 1.0e-5f);

    viewer::AreaRenderFrame frame;
    const uint8_t visible_chunks[] = {0u, 1u};
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = std::span<const uint8_t>{visible_chunks};
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    EXPECT_TRUE(frame.record_visible(record));
    EXPECT_EQ(frame.stats().visible_dynamic_record_count, 1u);
    ASSERT_TRUE(frame.has_visible_bounds());
    EXPECT_NEAR(frame.visible_bounds().min.x, 10.0f, 1.0e-5f);
    ASSERT_TRUE(frame.has_shadow_caster_bounds());
    EXPECT_NEAR(frame.shadow_caster_bounds().max.x, 12.0f, 1.0e-5f);
}

TEST(RenderAreaVisibility, VisibleRenderModelHandlesUseAreaFrameRecords)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 0u);
    scene.add(make_shadow_render_model());
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 0,
        .tile_y = 0,
    };
    scene.add(make_shadow_render_model());
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 1,
        .tile_y = 0,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    const uint32_t tile_record = area_scene.record_index_for_render_model(0u);
    const uint32_t visible_render_model_record = area_scene.record_index_for_render_model(1u);
    const uint32_t hidden_render_model_record = area_scene.record_index_for_render_model(2u);
    ASSERT_NE(tile_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(visible_render_model_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(hidden_render_model_record, viewer::kInvalidAreaRenderRecordIndex);

    viewer::AreaRenderFrame frame;
    const uint8_t visible_chunks[] = {1u, 0u};
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = std::span<const uint8_t>{visible_chunks};
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.record_visible(tile_record));
    ASSERT_TRUE(frame.record_visible(visible_render_model_record));
    ASSERT_FALSE(frame.record_visible(hidden_render_model_record));

    const auto frame_handles = frame.visible_render_model_instance_handles();
    ASSERT_EQ(frame_handles.size(), 2u);
    EXPECT_TRUE(frame_handles[0] == scene.static_model_instance_handles[0]);
    EXPECT_TRUE(frame_handles[1] == scene.static_model_instance_handles[1]);
}

TEST(RenderAreaVisibility, AreaStaticRecordsExposeCommonPreparedDraws)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_area_mesh_model(nw::render::MaterialMode::cutout);
    model->materials.front().material_uses_fallback = true;
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 6.0f, 0.0f});
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.stats().static_record_count, 1u);
    const auto common_draws = area_scene.prepared_model_draws_for_record(0u);
    const auto surfaces = area_scene.prepared_model_surface_draws_for_record(0u);
    ASSERT_EQ(common_draws.size(), 1u);
    ASSERT_EQ(surfaces.size(), 1u);

    const auto& draw = common_draws[0];
    EXPECT_TRUE(draw.instance == scene.static_model_instance_handles[0]);
    EXPECT_EQ(draw.instance_source_index, 0u);
    EXPECT_EQ(draw.source_draw_index, 0u);
    EXPECT_EQ(draw.material_mode, nw::render::MaterialMode::cutout);
    EXPECT_TRUE(draw.material_uses_fallback);
    EXPECT_EQ(draw.material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);
    EXPECT_FALSE(draw.skinned);
    EXPECT_NEAR(draw.world[3].x, 5.0f, 1.0e-5f);
    EXPECT_NEAR(draw.world[3].y, 6.0f, 1.0e-5f);
    const auto& surface = surfaces[0];
    EXPECT_EQ(surface.handle_index, 0u);
    EXPECT_EQ(surface.draw_index, 0u);
    EXPECT_EQ(surface.source_draw_index, draw.source_draw_index);
    EXPECT_EQ(surface.material_mode, draw.material_mode);
    EXPECT_TRUE(surface.material_uses_fallback);
    EXPECT_EQ(surface.material_payload, draw.material_payload);
    EXPECT_EQ(area_scene.prepared_model_surface_draws().stats.render_model_draw_count, 1u);
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(1u).empty());
    EXPECT_TRUE(area_scene.prepared_model_surface_draws_for_record(1u).empty());
    const auto& list = area_scene.prepared_model_draw_list();
    ASSERT_EQ(list.instance_offsets.size(), 2u);
    EXPECT_EQ(list.stats.handle_count, 1u);
    EXPECT_EQ(list.stats.render_model_instance_count, 1u);
    EXPECT_EQ(list.stats.render_model_draw_count, 1u);
    EXPECT_EQ(list.stats.material_fallback_draw_count, 1u);
    EXPECT_EQ(list.stats.render_model_material_fallback_draw_count, 1u);

    const auto& ranges = area_scene.prepared_model_draw_ranges();
    ASSERT_EQ(ranges.ranges.size(), 1u);
    EXPECT_EQ(ranges.stats.handle_count, 1u);
    EXPECT_EQ(ranges.stats.render_model_range_count, 1u);
    const auto range_draws = nw::render::prepared_model_draws_for_range(list, ranges.ranges[0]);
    ASSERT_EQ(range_draws.size(), 1u);
    EXPECT_EQ(range_draws[0].source_draw_index, draw.source_draw_index);
}

TEST(RenderAreaVisibility, CachedAreaVisibleSurfaceIndicesUseScenePassOrder)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 4;
    scene.area_height = 1;

    add_static_transparent_area_mesh_model(scene, 0u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::water, 1u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::cutout, 2u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 3u);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    expect_prepared_surface_stream_maps_to_draws(area_scene);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.uses_cached_draw_lists());
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto scene_indices = area_scene.prepared_surface_indices();
    const auto visible_indices = frame.visible_prepared_surface_indices();
    ASSERT_EQ(scene_indices.size(), 4u);
    ASSERT_EQ(visible_indices.size(), 4u);
    EXPECT_EQ(visible_indices.data(), scene_indices.data());
    for (const uint32_t surface_index : visible_indices) {
        ASSERT_LT(surface_index, surfaces.size());
    }
    EXPECT_EQ(surfaces[visible_indices[0]].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(surfaces[visible_indices[1]].material_mode, nw::render::MaterialMode::cutout);
    EXPECT_EQ(surfaces[visible_indices[2]].material_mode, nw::render::MaterialMode::water);
    EXPECT_EQ(surfaces[visible_indices[3]].material_mode, nw::render::MaterialMode::transparent);

    const auto opaque_cutout = frame.visible_opaque_cutout_prepared_surface_indices();
    ASSERT_EQ(opaque_cutout.size(), 2u);
    EXPECT_EQ(surfaces[opaque_cutout[0]].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(surfaces[opaque_cutout[1]].material_mode, nw::render::MaterialMode::cutout);
    const auto water = frame.visible_water_prepared_surface_indices();
    ASSERT_EQ(water.size(), 1u);
    EXPECT_EQ(surfaces[water[0]].material_mode, nw::render::MaterialMode::water);
    const auto transparent = frame.visible_transparent_prepared_surface_indices();
    ASSERT_EQ(transparent.size(), 1u);
    EXPECT_EQ(surfaces[transparent[0]].material_mode, nw::render::MaterialMode::transparent);
}

TEST(RenderAreaVisibility, FilteredAreaVisibleSurfaceIndicesUseCommonPassOrder)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 5;
    scene.area_height = 1;

    add_static_area_mesh_model(scene, nw::render::MaterialMode::water, 0u);
    add_static_transparent_area_mesh_model(scene, 1u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::cutout, 2u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 3u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 4u, false);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    expect_prepared_surface_stream_maps_to_draws(area_scene);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_FALSE(frame.uses_cached_draw_lists());
    EXPECT_EQ(frame.stats().visible_prepared_surface_count, 4u);
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto visible_indices = frame.visible_prepared_surface_indices();
    ASSERT_EQ(visible_indices.size(), 4u);
    for (const uint32_t surface_index : visible_indices) {
        ASSERT_LT(surface_index, surfaces.size());
    }
    EXPECT_EQ(surfaces[visible_indices[0]].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(surfaces[visible_indices[1]].material_mode, nw::render::MaterialMode::cutout);
    EXPECT_EQ(surfaces[visible_indices[2]].material_mode, nw::render::MaterialMode::water);
    EXPECT_EQ(surfaces[visible_indices[3]].material_mode, nw::render::MaterialMode::transparent);

    EXPECT_EQ(frame.visible_opaque_cutout_prepared_surface_indices().size(), 2u);
    EXPECT_EQ(frame.visible_water_prepared_surface_indices().size(), 1u);
    EXPECT_EQ(frame.visible_transparent_prepared_surface_indices().size(), 1u);
}

TEST(RenderAreaVisibility, NonCachedAreaVisibleMaterialListsUseCommonRecords)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    scene.add(make_area_mesh_model(nw::render::MaterialMode::cutout));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    auto disabled = make_area_mesh_model(nw::render::MaterialMode::opaque);
    scene.add(std::move(disabled));
    auto* disabled_instance = scene.static_model_instance(1u);
    ASSERT_NE(disabled_instance, nullptr);
    disabled_instance->visible = false;
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 1,
        .tile_y = 0,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_FALSE(frame.uses_cached_draw_lists());
    EXPECT_EQ(frame.stats().visible_prepared_surface_count, 1u);
    const auto cutout_surface_indices = frame.visible_cutout_prepared_surface_indices();
    ASSERT_EQ(cutout_surface_indices.size(), 1u);
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    ASSERT_LT(cutout_surface_indices[0], surfaces.size());
    EXPECT_EQ(surfaces[cutout_surface_indices[0]].source_draw_index, 0u);
    EXPECT_TRUE(frame.visible_opaque_prepared_surface_indices().empty());
}

TEST(RenderAreaVisibility, AreaPreparedSurfacesUseCommonShadowSummary)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    scene.add(make_area_mesh_model(nw::render::MaterialMode::opaque));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    auto caster = make_area_mesh_model(nw::render::MaterialMode::opaque);
    caster->primitives.front().casts_shadow = true;
    caster->shadow = nw::render::summarize_render_model_shadows(*caster);
    scene.add(std::move(caster));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 1,
        .tile_y = 0,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    const auto first_draws = area_scene.prepared_model_draws_for_record(0u);
    const auto first_surfaces = area_scene.prepared_model_surface_draws_for_record(0u);
    ASSERT_EQ(first_draws.size(), 1u);
    ASSERT_EQ(first_surfaces.size(), 1u);
    EXPECT_FALSE(first_draws[0].instance_casts_shadow);
    EXPECT_FALSE(first_surfaces[0].casts_shadow);

    const auto second_draws = area_scene.prepared_model_draws_for_record(1u);
    const auto second_surfaces = area_scene.prepared_model_surface_draws_for_record(1u);
    ASSERT_EQ(second_draws.size(), 1u);
    ASSERT_EQ(second_surfaces.size(), 1u);
    EXPECT_TRUE(second_draws[0].instance_casts_shadow);
    EXPECT_TRUE(second_surfaces[0].casts_shadow);
    EXPECT_EQ(area_scene.prepared_model_surface_draws().stats.shadow_caster_draw_count, 1u);
}

TEST(RenderAreaVisibility, AreaDynamicRecordsHaveEmptyCommonPreparedDrawSpans)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_area_mesh_model();
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(0u);
    ASSERT_NE(instance, nullptr);
    instance->scene_animation_enabled = true;
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.stats().dynamic_record_count, 1u);
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());
    EXPECT_EQ(area_scene.prepared_model_draw_list().stats.handle_count, 1u);
}

TEST(RenderAreaVisibility, NonCachedAreaVisibleMaterialListsDropStaleCommonRecords)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    scene.add(make_area_mesh_model(nw::render::MaterialMode::cutout));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    scene.model_instances.destroy(scene.static_model_instance_handles[0]);

    auto disabled = make_area_mesh_model(nw::render::MaterialMode::opaque);
    scene.add(std::move(disabled));
    auto* disabled_instance = scene.static_model_instance(1u);
    ASSERT_NE(disabled_instance, nullptr);
    disabled_instance->visible = false;
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 1,
        .tile_y = 0,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_FALSE(frame.uses_cached_draw_lists());
    EXPECT_EQ(frame.stats().visible_prepared_surface_count, 0u);
    EXPECT_TRUE(frame.visible_cutout_prepared_surface_indices().empty());
    EXPECT_TRUE(frame.visible_opaque_prepared_surface_indices().empty());
}

TEST(RenderAreaVisibility, AreaCommonPreparedDrawsDropStaleHandles)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    scene.add(make_area_mesh_model());
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .static_candidate = true,
    };
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    scene.model_instances.destroy(scene.static_model_instance_handles[0]);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.stats().static_record_count, 0u);
    ASSERT_EQ(area_scene.stats().dynamic_record_count, 1u);
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());
    EXPECT_TRUE(area_scene.opaque_prepared_surface_indices().empty());
    EXPECT_EQ(area_scene.prepared_model_draw_list().stats.stale_handle_count, 1u);
    EXPECT_TRUE(area_scene.prepared_model_draw_ranges().ranges.empty());
    EXPECT_EQ(area_scene.prepared_model_draw_ranges().stats.empty_range_count, 1u);
}

TEST(RenderAreaVisibility, StaleCommonInstanceHandleDropsAreaRecord)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_shadow_render_model();
    model->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    model->primitives.front().bounds = model->bounds;
    scene.add(std::move(model));

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.model_instance_handles().size(), 1u);

    scene.model_instances.destroy(scene.static_model_instance_handles[0]);
    area_scene.refresh_runtime_records(scene);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    EXPECT_FALSE(frame.record_visible(0u));
    EXPECT_EQ(frame.stats().visible_record_count, 0u);
    EXPECT_FALSE(frame.has_shadow_caster_bounds());
}

TEST(RenderAreaVisibility, ParticleOwnerRecordCullingIsOptIn)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    add_grid_area_tiles(scene, 5, 5);

    const size_t far_model_index = chunk_index(5u, 4u, 4u);
    auto& particle_system = scene.particles.emplace_back();
    particle_system.owner_model_index = static_cast<uint32_t>(far_model_index);
    particle_system.owner_instance_handle = scene.static_model_instance_handles[far_model_index];

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    std::vector<uint8_t> mask;
    viewer::rebuild_area_visibility_mask(
        area_scene,
        viewer::AreaVisibilityMaskOptions{
            .camera_position = {25.0f, 25.0f, 1.0f},
            .camera_target = {35.0f, 25.0f, 1.0f},
            .radius_tiles = 1,
            .mode = viewer::AreaVisibilityMaskMode::radius,
        },
        mask);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = mask;
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    EXPECT_TRUE(viewer::particle_system_visible_for_render_filter(
        particle_system,
        viewer::ParticleRenderFilter{
            .area_scene = &area_scene,
            .area_frame = &frame,
        }));
    EXPECT_FALSE(viewer::particle_system_visible_for_render_filter(
        particle_system,
        viewer::ParticleRenderFilter{
            .area_scene = &area_scene,
            .area_frame = &frame,
            .cull_by_owner_area_record = true,
        }));
}

TEST(RenderAreaVisibility, RenderModelParticleOwnerUsesRenderModelAreaRecord)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 0u);
    scene.add(make_shadow_render_model());
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 1,
        .tile_y = 0,
    };

    auto& particle_system = scene.particles.emplace_back();
    particle_system.owner_model_index = 1u;
    particle_system.owner_instance_handle = scene.static_model_instance_handles[1];

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    const uint32_t tile_record = area_scene.record_index_for_render_model(0u);
    const uint32_t render_model_record = area_scene.record_index_for_render_model(1u);
    ASSERT_NE(tile_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(render_model_record, viewer::kInvalidAreaRenderRecordIndex);

    viewer::AreaRenderFrame frame;
    const uint8_t visible_chunks[] = {1u, 0u};
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = std::span<const uint8_t>{visible_chunks};
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.record_visible(tile_record));
    ASSERT_FALSE(frame.record_visible(render_model_record));
    EXPECT_FALSE(viewer::particle_system_visible_for_render_filter(
        particle_system,
        viewer::ParticleRenderFilter{
            .area_scene = &area_scene,
            .area_frame = &frame,
            .cull_by_owner_area_record = true,
        }));
}

TEST(RenderAreaVisibility, ViewConeMaskDropsFarChunksBehindCamera)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    add_grid_area_tiles(scene, 5, 5);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    std::vector<uint8_t> mask;
    const size_t visible = viewer::rebuild_area_visibility_mask(
        area_scene,
        viewer::AreaVisibilityMaskOptions{
            .camera_position = {25.0f, 25.0f, 1.0f},
            .camera_target = {35.0f, 25.0f, 1.0f},
            .radius_tiles = 2,
            .half_angle_degrees = 45.0f,
            .mode = viewer::AreaVisibilityMaskMode::view_cone,
        },
        mask);

    ASSERT_EQ(mask.size(), 25u);
    EXPECT_GT(visible, 9u);
    EXPECT_LT(visible, 25u);
    EXPECT_EQ(mask[chunk_index(5u, 4u, 2u)], 1u);
    EXPECT_EQ(mask[chunk_index(5u, 1u, 2u)], 1u);
    EXPECT_EQ(mask[chunk_index(5u, 0u, 2u)], 0u);
}

TEST(RenderAreaVisibility, SortedStaticSurfaceListHeuristicAvoidsMostlyOccludedScenes)
{
    namespace viewer = nw::render::viewer;

    EXPECT_FALSE(viewer::should_use_sorted_area_static_surface_lists(0u, 0u));
    EXPECT_FALSE(viewer::should_use_sorted_area_static_surface_lists(4095u, 4095u));
    EXPECT_FALSE(viewer::should_use_sorted_area_static_surface_lists(4096u, 20000u));
    EXPECT_TRUE(viewer::should_use_sorted_area_static_surface_lists(5000u, 10000u));
    EXPECT_TRUE(viewer::should_use_sorted_area_static_surface_lists(6000u, 10000u));
    EXPECT_TRUE(viewer::should_use_sorted_area_static_surface_lists(4096u, 4096u));
}

TEST(RenderViewerTileLight, ParsesSlotSuffixes)
{
    namespace viewer = nw::render::viewer;

    const auto ml1 = viewer::parse_tile_light_slot("t01ml1");
    EXPECT_TRUE(ml1.valid);
    EXPECT_TRUE(ml1.is_main);
    EXPECT_FALSE(ml1.second);

    const auto ml2 = viewer::parse_tile_light_slot("TileMainLight_ml2");
    EXPECT_TRUE(ml2.valid);
    EXPECT_TRUE(ml2.is_main);
    EXPECT_TRUE(ml2.second);

    const auto sl1 = viewer::parse_tile_light_slot("sl1");
    EXPECT_TRUE(sl1.valid);
    EXPECT_FALSE(sl1.is_main);
    EXPECT_FALSE(sl1.second);

    const auto sl2 = viewer::parse_tile_light_slot("t01sl2");
    EXPECT_TRUE(sl2.valid);
    EXPECT_FALSE(sl2.is_main);
    EXPECT_TRUE(sl2.second);

    EXPECT_FALSE(viewer::parse_tile_light_slot("t01ml3").valid);
    EXPECT_FALSE(viewer::parse_tile_light_slot("t01xx1").valid);
    EXPECT_FALSE(viewer::parse_tile_light_slot("").valid);
    EXPECT_FALSE(viewer::parse_tile_light_slot("ml").valid);
}

TEST(RenderViewerTileLight, SelectsSlotColorIndex)
{
    namespace viewer = nw::render::viewer;

    const viewer::SceneTileLightSlots slots{
        .main1 = 3,
        .main2 = 4,
        .source1 = 5,
        .source2 = 6,
    };

    EXPECT_EQ(viewer::tile_slot_color_index(slots, viewer::TileLightSlot{true, true, false}), 3u);
    EXPECT_EQ(viewer::tile_slot_color_index(slots, viewer::TileLightSlot{true, true, true}), 4u);
    EXPECT_EQ(viewer::tile_slot_color_index(slots, viewer::TileLightSlot{true, false, false}), 5u);
    EXPECT_EQ(viewer::tile_slot_color_index(slots, viewer::TileLightSlot{true, false, true}), 6u);
}

TEST(RenderViewerTileLight, LooksUpTileColor)
{
    namespace viewer = nw::render::viewer;

    // Row 3: (0.49, 0.29, 0.07).
    const glm::vec3 row3 = viewer::tile_color_from_index(3);
    EXPECT_NEAR(row3.r, 0.49f, 1.0e-5f);
    EXPECT_NEAR(row3.g, 0.29f, 1.0e-5f);
    EXPECT_NEAR(row3.b, 0.07f, 1.0e-5f);

    // Row 2: (0.43, 0.06, 0.06).
    const glm::vec3 row2 = viewer::tile_color_from_index(2);
    EXPECT_NEAR(row2.r, 0.43f, 1.0e-5f);
    EXPECT_NEAR(row2.g, 0.06f, 1.0e-5f);
    EXPECT_NEAR(row2.b, 0.06f, 1.0e-5f);

    // Out-of-range indices clamp to the last row (15: white).
    const glm::vec3 clamped = viewer::tile_color_from_index(200);
    EXPECT_NEAR(clamped.r, 1.0f, 1.0e-5f);
    EXPECT_NEAR(clamped.g, 1.0f, 1.0e-5f);
    EXPECT_NEAR(clamped.b, 1.0f, 1.0e-5f);
}

TEST(RenderViewerTileLight, ResolvesModelLightNodeSlotColor)
{
    namespace viewer = nw::render::viewer;

    const viewer::SceneTileLightSlots slots{
        .main1 = 3,
        .main2 = 4,
        .source1 = 5,
        .source2 = 6,
    };

    // Large-radius ml node -> main slot, so ml1 reads main1 (row 3).
    nw::model::LightNode main_light{"t01ml1"};
    main_light.flareradius = 14.0f;
    const glm::vec3 main_color = viewer::tile_slot_color_for_model_light(main_light, slots);
    EXPECT_NEAR(main_color.r, 0.49f, 1.0e-5f);
    EXPECT_NEAR(main_color.g, 0.29f, 1.0e-5f);
    EXPECT_NEAR(main_color.b, 0.07f, 1.0e-5f);

    // Small-radius ml node -> source slot, so ml2 reads source2 (row 6).
    nw::model::LightNode source_light{"t01ml2"};
    source_light.flareradius = 5.0f;
    const glm::vec3 source_color = viewer::tile_slot_color_for_model_light(source_light, slots);
    EXPECT_NEAR(source_color.r, 0.55f, 1.0e-5f);
    EXPECT_NEAR(source_color.g, 0.66f, 1.0e-5f);
    EXPECT_NEAR(source_color.b, 0.40f, 1.0e-5f);

    // Explicit sl node -> source slot regardless of radius, so sl1 reads source1 (row 5).
    nw::model::LightNode explicit_source_light{"t01sl1"};
    explicit_source_light.flareradius = 14.0f;
    const glm::vec3 explicit_source_color = viewer::tile_slot_color_for_model_light(explicit_source_light, slots);
    EXPECT_NEAR(explicit_source_color.r, 0.86f, 1.0e-5f);
    EXPECT_NEAR(explicit_source_color.g, 0.84f, 1.0e-5f);
    EXPECT_NEAR(explicit_source_color.b, 0.59f, 1.0e-5f);

    nw::model::LightNode unknown_light{"random_node"};
    const glm::vec3 unknown = viewer::tile_slot_color_for_model_light(unknown_light, slots);
    EXPECT_NEAR(unknown.r, 0.0f, 1.0e-5f);
    EXPECT_NEAR(unknown.g, 0.0f, 1.0e-5f);
    EXPECT_NEAR(unknown.b, 0.0f, 1.0e-5f);
}
