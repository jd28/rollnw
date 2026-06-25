#include <gtest/gtest.h>

#include <nw/render/viewer/area_lighting.hpp>
#include <nw/render/viewer/area_render_scene.hpp>
#include <nw/render/viewer/camera.hpp>
#include <nw/render/viewer/preview_model_draws.hpp>
#include <nw/render/viewer/preview_scene.hpp>
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

void add_grid_area_tiles(nw::render::viewer::PreviewScene& scene, int width, int height)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    scene.is_area = true;
    scene.area_width = static_cast<uint32_t>(std::max(width, 0));
    scene.area_height = static_cast<uint32_t>(std::max(height, 0));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto tile = std::make_unique<nwn::ModelInstance>();
            const float min_x = static_cast<float>(x) * viewer::kAreaRenderTileSize;
            const float min_y = static_cast<float>(y) * viewer::kAreaRenderTileSize;
            tile->bounds = nw::render::Bounds{
                .min = {min_x, min_y, 0.0f},
                .max = {min_x + viewer::kAreaRenderTileSize, min_y + viewer::kAreaRenderTileSize, 1.0f},
            };
            tile->material_pass_mask = 0x1u;
            scene.add(std::move(tile));
            scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
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

void expect_surface_batch_maps_to_draws(
    const nw::render::viewer::AreaRenderScene& area_scene,
    const nw::render::viewer::AreaStaticMaterialDrawBatch& batch)
{
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto draw_items = area_scene.prepared_draws();

    for (const uint32_t surface_index : batch.surface_indices) {
        ASSERT_LT(surface_index, surfaces.size());
        const auto& surface = surfaces[surface_index];
        EXPECT_EQ(surface.payload_kind, nw::render::PreparedModelDrawKind::nwn_legacy);
        EXPECT_EQ(surface.instance_kind, nw::render::ModelInstanceKind::nwn_legacy);
        EXPECT_EQ(surface.material_mode, batch.material);
        ASSERT_LT(surface.source_draw_index, draw_items.size());
    }
}

void expect_static_material_cache_stream_maps_to_draws(
    const nw::render::viewer::AreaRenderScene& area_scene)
{
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto draw_items = area_scene.prepared_draws();
    const auto surface_indices = area_scene.static_material_prepared_surface_indices();

    for (size_t i = 0; i < surface_indices.size(); ++i) {
        const uint32_t surface_index = surface_indices[i];
        ASSERT_LT(surface_index, surfaces.size());
        const auto& surface = surfaces[surface_index];
        EXPECT_EQ(surface.payload_kind, nw::render::PreparedModelDrawKind::nwn_legacy);
        ASSERT_LT(surface.source_draw_index, draw_items.size());
        EXPECT_EQ(draw_items[surface.source_draw_index].static_material_index, static_cast<uint32_t>(i));
    }
}

std::unique_ptr<nw::render::nwn::ModelInstance> make_area_mesh_model(
    nw::render::MaterialMode material_mode = nw::render::MaterialMode::opaque)
{
    namespace nwn = nw::render::nwn;

    auto model = std::make_unique<nwn::ModelInstance>();
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {2.0f, 3.0f, 4.0f},
    };
    model->material_pass_mask = material_mode == nw::render::MaterialMode::water ? 0x2u : 0x1u;
    model->scene_animation_enabled = false;

    auto mesh = std::make_unique<nwn::Mesh>();
    mesh->owner_ = model.get();
    mesh->owns_gpu_buffers = false;
    mesh->vertices = dummy_buffer_handle();
    mesh->indices = dummy_buffer_handle();
    mesh->vertex_count = 3;
    mesh->index_count = 3;
    mesh->material_mode = material_mode;
    mesh->local_bounds = model->bounds;
    model->nodes_.push_back(std::move(mesh));

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
    model->render_enabled = render_enabled;
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
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
    scene.models.back()->material_pass_mask = 0x4u;
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

TEST(RenderViewerLighting, DynamicLocalLightFollowsTrackedModelNode)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nwn::ModelInstance>();
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 30.0f}));

    auto node = std::make_unique<nwn::Node>();
    node->has_transform_ = true;
    node->position_ = glm::vec3{1.0f, 2.0f, 3.0f};
    auto* node_ptr = node.get();
    model->nodes_.push_back(std::move(node));
    model->source_nodes_.push_back(node_ptr);
    scene.add(std::move(model));

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
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nwn::ModelInstance>();
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 30.0f}));

    auto node = std::make_unique<nwn::Node>();
    node->has_transform_ = true;
    node->position_ = glm::vec3{1.0f, 2.0f, 3.0f};
    auto* node_ptr = node.get();
    model->nodes_.push_back(std::move(node));
    model->source_nodes_.push_back(node_ptr);
    scene.add(std::move(model));

    auto* instance = scene.nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{100.0f, 200.0f, 300.0f});

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
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nwn::ModelInstance>();
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 30.0f}));

    auto node = std::make_unique<nwn::Node>();
    node->has_transform_ = true;
    node->position_ = glm::vec3{1.0f, 2.0f, 3.0f};
    auto* node_ptr = node.get();
    model->nodes_.push_back(std::move(node));
    model->source_nodes_.push_back(node_ptr);
    scene.add(std::move(model));
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);

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
    scene.model_instances.destroy(scene.model_instance_handles[0]);

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

TEST(RenderViewerLighting, FilteredAreaFrameKeepsModelBoundDynamicLights)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    auto first = std::make_unique<nwn::ModelInstance>();
    first->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    first->material_pass_mask = 0x1u;
    scene.add(std::move(first));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    auto second = std::make_unique<nwn::ModelInstance>();
    second->bounds = nw::render::Bounds{.min = {10.0f, 0.0f, 0.0f}, .max = {11.0f, 1.0f, 1.0f}};
    second->material_pass_mask = 0x1u;
    scene.add(std::move(second));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
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

TEST(RenderViewerShadow, LegacyCommonInstanceTracksVisibilityAndShadowSummary)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nwn::ModelInstance>();
    model->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    model->shadow_casters_.push_back(nullptr);
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    scene.add(std::move(model));

    ASSERT_EQ(scene.model_instance_handles.size(), 1u);
    auto* instance = scene.nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->kind, nw::render::ModelInstanceKind::nwn_legacy);
    EXPECT_EQ(instance->nwn_legacy_model_index, 0u);
    EXPECT_TRUE(instance->visible);
    EXPECT_TRUE(instance->shadow.casts_shadow);
    EXPECT_EQ(instance->shadow.caster_count, 1u);
    EXPECT_NEAR(instance->shadow.bounds.min.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(instance->root_transform[3].x, 2.0f, 1.0e-5f);

    scene.models[0]->render_enabled = false;
    scene.models[0]->shadow_casters_.clear();
    scene.update(0);

    instance = scene.nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->visible);
    EXPECT_FALSE(instance->shadow.casts_shadow);
    EXPECT_EQ(instance->shadow.caster_count, 0u);
    EXPECT_NEAR(instance->root_transform[3].x, 2.0f, 1.0e-5f);

    auto* raw_instance = scene.model_instances.get(scene.model_instance_handles[0]);
    ASSERT_NE(raw_instance, nullptr);
    raw_instance->nwn_legacy_model_index = 9u;
    EXPECT_EQ(scene.nwn_model_instance(0), nullptr);

    viewer::PreviewScene stale_scene;
    stale_scene.add(std::make_unique<nwn::ModelInstance>());
    ASSERT_EQ(stale_scene.model_instance_handles.size(), 1u);
    stale_scene.model_instances.destroy(stale_scene.model_instance_handles[0]);
    EXPECT_EQ(stale_scene.nwn_model_instance(0), nullptr);
}

TEST(RenderViewerShadow, SceneModelAttachmentBindingDrivesCommonRoot)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;

    auto owner = std::make_unique<nwn::ModelInstance>();
    owner->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    auto anchor_node = std::make_unique<nwn::Node>();
    anchor_node->has_transform_ = true;
    anchor_node->position_ = glm::vec3{2.0f, 3.0f, 4.0f};
    auto* anchor_node_ptr = anchor_node.get();
    owner->nodes_.push_back(std::move(anchor_node));
    owner->source_nodes_.push_back(anchor_node_ptr);
    owner->sockets_.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = anchor_node_ptr->get_local_transform(),
        .bind_transform = anchor_node_ptr->bind_pose_,
        .name = "hand",
    });
    scene.add(std::move(owner));
    ASSERT_EQ(scene.models.size(), 1u);

    auto child = std::make_unique<nwn::ModelInstance>();
    child->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    scene.add_attached(std::move(child), 0u, "hand");

    ASSERT_EQ(scene.model_attachments.size(), 1u);
    const auto& binding = scene.model_attachments.front();
    EXPECT_EQ(binding.child_model_index, 1u);
    EXPECT_EQ(binding.owner_model_index, 0u);
    EXPECT_EQ(binding.owner_instance_handle, scene.model_instance_handles[0]);
    EXPECT_EQ(binding.child_instance_handle, scene.model_instance_handles[1]);
    EXPECT_NE(binding.owner_socket_index, nw::render::kInvalidModelNodeIndex);

    scene.models[1]->transform_context_ = nullptr;
    scene.models[0]->sockets_.front().name = "renamed_after_binding";
    viewer::sync_model_instance_runtime_state(scene);

    const auto* child_instance = scene.nwn_model_instance(1);
    ASSERT_NE(child_instance, nullptr);
    EXPECT_NEAR(child_instance->root_transform[3].x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].z, 4.0f, 1.0e-5f);
}

TEST(RenderViewerShadow, SceneAddAttachedWithInvalidOwnerAddsUnboundModel)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    auto child = std::make_unique<nwn::ModelInstance>();
    child->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};

    scene.add_attached(std::move(child), 4u, "hand");

    ASSERT_EQ(scene.models.size(), 1u);
    EXPECT_TRUE(scene.model_attachments.empty());
    EXPECT_EQ(scene.models.front()->transform_context_, nullptr);
}

TEST(RenderViewerShadow, LegacyPreparedSurfaceDropsWaterShadowCaster)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = make_area_mesh_model(nw::render::MaterialMode::water);
    ASSERT_FALSE(model->nodes_.empty());
    auto* shadow_mesh = static_cast<nwn::Mesh*>(model->nodes_.back().get());
    model->shadow_casters_.push_back(shadow_mesh);
    scene.add(std::move(model));

    const auto* instance = scene.nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(instance->shadow.casts_shadow);

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
    EXPECT_EQ(surfaces.draws[0].payload_kind, nw::render::PreparedModelDrawKind::nwn_legacy);
    EXPECT_EQ(surfaces.draws[0].material_mode, nw::render::MaterialMode::water);
    EXPECT_FALSE(surfaces.draws[0].casts_shadow);
}

TEST(RenderViewerShadow, PreparedSurfacesUseCommonInstanceShadowSummary)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_area_mesh_model(nw::render::MaterialMode::opaque));
    scene.add(make_shadow_render_model());

    viewer::PreviewPreparedModelDraws prepared;
    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);

    ASSERT_EQ(surfaces.draws.size(), 2u);
    EXPECT_EQ(surfaces.stats.shadow_caster_draw_count, 1u);

    const auto legacy = std::find_if(
        surfaces.draws.begin(),
        surfaces.draws.end(),
        [](const auto& surface) {
            return surface.payload_kind == nw::render::PreparedModelDrawKind::nwn_legacy;
        });
    const auto render_model = std::find_if(
        surfaces.draws.begin(),
        surfaces.draws.end(),
        [](const auto& surface) {
            return surface.payload_kind == nw::render::PreparedModelDrawKind::render_model;
        });
    ASSERT_NE(legacy, surfaces.draws.end());
    ASSERT_NE(render_model, surfaces.draws.end());
    ASSERT_LT(legacy->draw_index, prepared.common.draws.size());
    ASSERT_LT(render_model->draw_index, prepared.common.draws.size());
    EXPECT_FALSE(prepared.common.draws[legacy->draw_index].instance_casts_shadow);
    EXPECT_TRUE(prepared.common.draws[render_model->draw_index].instance_casts_shadow);
    EXPECT_FALSE(legacy->casts_shadow);
    EXPECT_TRUE(render_model->casts_shadow);

    auto* render_instance = scene.static_model_instance(0);
    ASSERT_NE(render_instance, nullptr);
    render_instance->shadow.casts_shadow = false;
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
    scene.models[chunk_index(5u, 2u, 2u)]->shadow_casters_.push_back(nullptr);
    scene.models[chunk_index(5u, 4u, 4u)]->shadow_casters_.push_back(nullptr);
    viewer::sync_model_instance_runtime_state(scene);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.stats().chunk_width, 5u);
    ASSERT_EQ(area_scene.stats().chunk_height, 5u);
    const uint32_t center_record = area_scene.record_index_for_model(static_cast<uint32_t>(chunk_index(5u, 2u, 2u)));
    const uint32_t far_record = area_scene.record_index_for_model(static_cast<uint32_t>(chunk_index(5u, 4u, 4u)));
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
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 5;
    scene.area_height = 1;

    auto model = std::make_unique<nwn::ModelInstance>();
    model->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    model->material_pass_mask = 0x1u;
    model->shadow_casters_.push_back(nullptr);
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.stats().dynamic_record_count, 1u);
    ASSERT_EQ(area_scene.model_instance_handles().size(), 1u);

    auto* instance = scene.nwn_model_instance(0);
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
    ASSERT_EQ(area_scene.model_kinds().size(), 1u);
    EXPECT_EQ(area_scene.model_kinds()[record], nw::render::ModelInstanceKind::render_model);
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
    const uint32_t legacy_record = area_scene.record_index_for_model(0u);
    const uint32_t visible_render_model_record = area_scene.record_index_for_render_model(0u);
    const uint32_t hidden_render_model_record = area_scene.record_index_for_render_model(1u);
    ASSERT_NE(legacy_record, viewer::kInvalidAreaRenderRecordIndex);
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

    ASSERT_TRUE(frame.record_visible(legacy_record));
    ASSERT_TRUE(frame.record_visible(visible_render_model_record));
    ASSERT_FALSE(frame.record_visible(hidden_render_model_record));

    const auto frame_handles = frame.visible_render_model_instance_handles();
    ASSERT_EQ(frame_handles.size(), 1u);
    EXPECT_TRUE(frame_handles[0] == scene.static_model_instance_handles[0]);
}

TEST(RenderAreaVisibility, DirectModelRecordSelectionUsesSelectedAreaRecords)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 3;
    scene.area_height = 1;

    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 0u);
    scene.add(make_area_mesh_model(nw::render::MaterialMode::opaque));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 0,
        .tile_y = 0,
    };
    scene.add(make_shadow_render_model(nw::render::MaterialMode::opaque));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 0,
        .tile_y = 0,
    };
    scene.add(make_shadow_render_model(nw::render::MaterialMode::water));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 1,
        .tile_y = 0,
    };
    scene.add(make_shadow_render_model(nw::render::MaterialMode::transparent));
    scene.static_area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .tile_x = 2,
        .tile_y = 0,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    const uint32_t static_legacy_record = area_scene.record_index_for_model(0u);
    const uint32_t dynamic_legacy_record = area_scene.record_index_for_model(1u);
    const uint32_t opaque_record = area_scene.record_index_for_render_model(0u);
    const uint32_t water_record = area_scene.record_index_for_render_model(1u);
    const uint32_t transparent_record = area_scene.record_index_for_render_model(2u);
    ASSERT_NE(static_legacy_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(dynamic_legacy_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(opaque_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(water_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(transparent_record, viewer::kInvalidAreaRenderRecordIndex);

    viewer::AreaRenderFrame frame;
    const uint8_t visible_chunks[] = {1u, 1u, 0u};
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = std::span<const uint8_t>{visible_chunks};
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    const auto visible_nwn_handles = frame.visible_nwn_legacy_instance_handles();
    ASSERT_EQ(visible_nwn_handles.size(), 1u);
    EXPECT_TRUE(visible_nwn_handles[0] == scene.model_instance_handles[1]);
    const auto expect_direct_records =
        [](const viewer::AreaDirectModelRecordSelection& records,
            std::initializer_list<viewer::AreaDirectModelRecord> expected) {
            ASSERT_EQ(records.records.size(), expected.size());
            size_t index = 0;
            for (const auto& expected_record : expected) {
                EXPECT_EQ(records.records[index].kind, expected_record.kind);
                EXPECT_EQ(records.records[index].record_index, expected_record.record_index);
                ++index;
            }
        };

    const auto& frame_opaque_records = frame.direct_model_records_for_pass(nw::render::RenderPassSelection::opaque_cutout);
    expect_direct_records(
        frame_opaque_records,
        {
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::nwn_legacy,
                .record_index = dynamic_legacy_record,
            },
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::render_model,
                .record_index = opaque_record,
            },
        });

    const auto& frame_water_records = frame.direct_model_records_for_pass(nw::render::RenderPassSelection::water);
    expect_direct_records(
        frame_water_records,
        {
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::render_model,
                .record_index = water_record,
            },
        });

    const auto& frame_transparent_records = frame.direct_model_records_for_pass(nw::render::RenderPassSelection::transparent);
    expect_direct_records(frame_transparent_records, {});

    const auto& frame_all_records = frame.direct_model_records_for_pass(nw::render::RenderPassSelection::all);
    expect_direct_records(
        frame_all_records,
        {
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::nwn_legacy,
                .record_index = dynamic_legacy_record,
            },
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::render_model,
                .record_index = opaque_record,
            },
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::render_model,
                .record_index = water_record,
            },
        });

    nw::render::ModelRenderContext render_model_ctx;
    const auto merge_render_model_stats =
        [](viewer::AreaDirectModelSubmissionStats& target,
            const viewer::AreaDirectModelSubmissionStats& source) {
            target.selected_render_model_record_count += source.selected_render_model_record_count;
            target.dropped_invalid_record_count += source.dropped_invalid_record_count;
            target.dropped_invalid_source_count += source.dropped_invalid_source_count;
            target.dropped_unsupported_kind_count += source.dropped_unsupported_kind_count;
        };

    auto direct_stats = viewer::collect_area_direct_legacy_model_records(
        area_scene,
        frame_all_records,
        viewer::AreaDirectRenderModelRecordPolicy::ignore);
    merge_render_model_stats(
        direct_stats,
        viewer::render_area_direct_render_model_records(
            render_model_ctx,
            nullptr,
            scene,
            area_scene,
            frame_all_records,
            nw::render::RenderContext{},
            nw::render::RenderPassSelection::all));
    EXPECT_TRUE(direct_stats.valid());
    EXPECT_EQ(direct_stats.input_record_count, 3u);
    EXPECT_EQ(direct_stats.selected_legacy_record_count, 1u);
    EXPECT_EQ(direct_stats.selected_render_model_record_count, 2u);

    const auto legacy_only_stats = viewer::collect_area_direct_legacy_model_records(
        area_scene,
        frame_all_records,
        viewer::AreaDirectRenderModelRecordPolicy::count_skipped);
    EXPECT_TRUE(legacy_only_stats.valid());
    EXPECT_EQ(legacy_only_stats.input_record_count, 3u);
    EXPECT_EQ(legacy_only_stats.selected_legacy_record_count, 1u);
    EXPECT_EQ(legacy_only_stats.selected_render_model_record_count, 0u);
    EXPECT_EQ(legacy_only_stats.skipped_render_model_record_count, 2u);

    viewer::AreaRenderFrame prepared_render_model_frame;
    cull.collect_direct_render_model_records = false;
    prepare_area_frame(area_scene, prepared_render_model_frame, cull);

    const auto prepared_visible_nwn_handles = prepared_render_model_frame.visible_nwn_legacy_instance_handles();
    ASSERT_EQ(prepared_visible_nwn_handles.size(), 1u);
    EXPECT_TRUE(prepared_visible_nwn_handles[0] == scene.model_instance_handles[1]);
    const auto& prepared_frame_opaque_records = prepared_render_model_frame.direct_model_records_for_pass(
        nw::render::RenderPassSelection::opaque_cutout);
    expect_direct_records(
        prepared_frame_opaque_records,
        {
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::nwn_legacy,
                .record_index = dynamic_legacy_record,
            },
        });
    EXPECT_TRUE(prepared_render_model_frame.direct_model_records_for_pass(
        nw::render::RenderPassSelection::water).records.empty());
    EXPECT_TRUE(prepared_render_model_frame.direct_model_records_for_pass(
        nw::render::RenderPassSelection::transparent).records.empty());
    const auto& prepared_frame_all_records = prepared_render_model_frame.direct_model_records_for_pass(
        nw::render::RenderPassSelection::all);
    expect_direct_records(
        prepared_frame_all_records,
        {
            viewer::AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::nwn_legacy,
                .record_index = dynamic_legacy_record,
            },
        });

    const auto prepared_direct_stats = viewer::collect_area_direct_legacy_model_records(
        area_scene,
        prepared_frame_all_records,
        viewer::AreaDirectRenderModelRecordPolicy::count_skipped);
    EXPECT_TRUE(prepared_direct_stats.valid());
    EXPECT_EQ(prepared_direct_stats.input_record_count, 1u);
    EXPECT_EQ(prepared_direct_stats.selected_legacy_record_count, 1u);
    EXPECT_EQ(prepared_direct_stats.skipped_render_model_record_count, 0u);
}

TEST(RenderAreaVisibility, AreaStaticRecordsExposeCommonPreparedDraws)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_area_mesh_model(nw::render::MaterialMode::cutout);
    auto* mesh = static_cast<nw::render::nwn::Mesh*>(model->nodes_.back().get());
    mesh->material_uses_fallback = true;
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 6.0f, 0.0f}));
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.stats().static_record_count, 1u);
    const auto sidecar_draws = area_scene.prepared_draws_for_record(0u);
    const auto common_draws = area_scene.prepared_model_draws_for_record(0u);
    const auto surfaces = area_scene.prepared_model_surface_draws_for_record(0u);
    ASSERT_EQ(sidecar_draws.size(), 1u);
    ASSERT_EQ(common_draws.size(), 1u);
    ASSERT_EQ(surfaces.size(), 1u);

    const auto& draw = common_draws[0];
    EXPECT_TRUE(draw.instance == scene.model_instance_handles[0]);
    EXPECT_EQ(draw.instance_kind, nw::render::ModelInstanceKind::nwn_legacy);
    EXPECT_EQ(draw.kind, nw::render::PreparedModelDrawKind::nwn_legacy);
    EXPECT_EQ(draw.instance_source_index, 0u);
    EXPECT_EQ(draw.source_draw_index, 0u);
    EXPECT_EQ(draw.material_mode, nw::render::MaterialMode::cutout);
    EXPECT_TRUE(draw.material_uses_fallback);
    EXPECT_EQ(draw.material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);
    EXPECT_FALSE(draw.skinned);
    EXPECT_NEAR(draw.world[3].x, sidecar_draws[0].world[3].x, 1.0e-5f);
    EXPECT_NEAR(draw.world[3].y, sidecar_draws[0].world[3].y, 1.0e-5f);
    EXPECT_NEAR(draw.bounds.max.z, sidecar_draws[0].light_bounds.max.z, 1.0e-5f);
    const auto& surface = surfaces[0];
    EXPECT_EQ(surface.handle_index, 0u);
    EXPECT_EQ(surface.draw_index, 0u);
    EXPECT_EQ(surface.payload_kind, nw::render::PreparedModelDrawKind::nwn_legacy);
    EXPECT_EQ(surface.source_draw_index, draw.source_draw_index);
    EXPECT_EQ(surface.material_mode, draw.material_mode);
    EXPECT_TRUE(surface.material_uses_fallback);
    EXPECT_EQ(surface.material_payload, draw.material_payload);
    EXPECT_EQ(area_scene.prepared_model_surface_draws().stats.nwn_legacy_draw_count, 1u);
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(1u).empty());
    EXPECT_TRUE(area_scene.prepared_model_surface_draws_for_record(1u).empty());
    const auto cutout_material_surface_indices = area_scene.cutout_static_prepared_surface_indices();
    ASSERT_EQ(cutout_material_surface_indices.size(), 1u);
    ASSERT_LT(cutout_material_surface_indices[0], surfaces.size());
    EXPECT_EQ(surfaces[cutout_material_surface_indices[0]].source_draw_index, draw.source_draw_index);
    EXPECT_TRUE(area_scene.opaque_static_prepared_surface_indices().empty());

    const auto& list = area_scene.prepared_model_draw_list();
    ASSERT_EQ(list.instance_offsets.size(), 2u);
    EXPECT_EQ(list.stats.handle_count, 1u);
    EXPECT_EQ(list.stats.nwn_legacy_instance_count, 1u);
    EXPECT_EQ(list.stats.nwn_legacy_draw_count, 1u);
    EXPECT_EQ(list.stats.material_fallback_draw_count, 1u);
    EXPECT_EQ(list.stats.render_model_material_fallback_draw_count, 0u);
    EXPECT_EQ(list.stats.nwn_legacy_material_fallback_draw_count, 1u);

    const auto& ranges = area_scene.prepared_model_draw_ranges();
    ASSERT_EQ(ranges.ranges.size(), 1u);
    EXPECT_EQ(ranges.stats.handle_count, 1u);
    EXPECT_EQ(ranges.stats.nwn_legacy_range_count, 1u);
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
    expect_static_material_cache_stream_maps_to_draws(area_scene);

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

TEST(RenderAreaVisibility, AreaStaticMaterialBatchesDescribeLegacyPasses)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 4;
    scene.area_height = 1;

    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, 0u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::cutout, 1u);
    add_static_area_mesh_model(scene, nw::render::MaterialMode::water, 2u);
    add_static_transparent_area_mesh_model(scene, 3u);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    expect_static_material_cache_stream_maps_to_draws(area_scene);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    const nw::gfx::StorageSpan cached_draw_data{dummy_buffer_handle(), 4u, 8u};
    const auto opaque_cutout = viewer::area_static_material_draw_batches_for_pass(
        frame, nw::render::RenderPassSelection::opaque_cutout, cached_draw_data);
    const auto opaque_submission_surfaces = frame.uses_sorted_static_draw_lists()
        ? frame.visible_opaque_static_prepared_surface_indices()
        : frame.visible_opaque_prepared_surface_indices();
    const auto cutout_submission_surfaces = frame.uses_sorted_static_draw_lists()
        ? frame.visible_cutout_static_prepared_surface_indices()
        : frame.visible_cutout_prepared_surface_indices();
    const auto opaque_cutout_batches = opaque_cutout.span();
    ASSERT_EQ(opaque_cutout_batches.size(), 2u);
    EXPECT_EQ(opaque_cutout_batches[0].material, nw::render::MaterialMode::opaque);
    EXPECT_EQ(opaque_cutout_batches[0].surface_indices.data(), opaque_submission_surfaces.data());
    EXPECT_EQ(opaque_cutout_batches[0].surface_indices.size(), opaque_submission_surfaces.size());
    expect_surface_batch_maps_to_draws(area_scene, opaque_cutout_batches[0]);
    EXPECT_EQ(opaque_cutout_batches[0].static_geometry_sorted, frame.uses_sorted_static_draw_lists());
    EXPECT_EQ(opaque_cutout_batches[0].cached_draw_data.buffer, cached_draw_data.buffer);
    EXPECT_EQ(opaque_cutout_batches[0].cached_draw_data.offset, 4u);
    EXPECT_EQ(opaque_cutout_batches[0].cached_draw_data.size, 8u);
    EXPECT_EQ(opaque_cutout_batches[1].material, nw::render::MaterialMode::cutout);
    EXPECT_EQ(opaque_cutout_batches[1].surface_indices.data(), cutout_submission_surfaces.data());
    EXPECT_EQ(opaque_cutout_batches[1].surface_indices.size(), cutout_submission_surfaces.size());
    expect_surface_batch_maps_to_draws(area_scene, opaque_cutout_batches[1]);
    EXPECT_EQ(opaque_cutout_batches[1].static_geometry_sorted, frame.uses_sorted_static_draw_lists());
    EXPECT_EQ(opaque_cutout_batches[1].cached_draw_data.buffer, cached_draw_data.buffer);

    const auto depth_prepass = viewer::area_static_material_depth_prepass_batch(frame, cached_draw_data);
    EXPECT_EQ(depth_prepass.material, nw::render::MaterialMode::opaque);
    EXPECT_EQ(depth_prepass.surface_indices.data(), opaque_submission_surfaces.data());
    EXPECT_EQ(depth_prepass.surface_indices.size(), opaque_submission_surfaces.size());
    expect_surface_batch_maps_to_draws(area_scene, depth_prepass);
    EXPECT_EQ(depth_prepass.static_geometry_sorted, frame.uses_sorted_static_draw_lists());
    EXPECT_EQ(depth_prepass.cached_draw_data.buffer, cached_draw_data.buffer);
    EXPECT_EQ(depth_prepass.cached_draw_data.offset, 4u);
    EXPECT_EQ(depth_prepass.cached_draw_data.size, 8u);

    const auto water = viewer::area_static_material_draw_batches_for_pass(
        frame, nw::render::RenderPassSelection::water);
    ASSERT_EQ(water.span().size(), 1u);
    EXPECT_EQ(water.span()[0].material, nw::render::MaterialMode::water);
    EXPECT_EQ(water.span()[0].surface_indices.data(), frame.visible_water_prepared_surface_indices().data());
    EXPECT_EQ(water.span()[0].surface_indices.size(), frame.visible_water_prepared_surface_indices().size());
    expect_surface_batch_maps_to_draws(area_scene, water.span()[0]);
    EXPECT_FALSE(water.span()[0].static_geometry_sorted);
    EXPECT_FALSE(water.span()[0].cached_draw_data.buffer.valid());

    const auto transparent = viewer::area_static_material_draw_batches_for_pass(
        frame, nw::render::RenderPassSelection::transparent);
    ASSERT_EQ(transparent.span().size(), 1u);
    EXPECT_EQ(transparent.span()[0].material, nw::render::MaterialMode::transparent);
    EXPECT_EQ(transparent.span()[0].surface_indices.data(), frame.visible_transparent_prepared_surface_indices().data());
    EXPECT_EQ(transparent.span()[0].surface_indices.size(), frame.visible_transparent_prepared_surface_indices().size());
    expect_surface_batch_maps_to_draws(area_scene, transparent.span()[0]);
    EXPECT_FALSE(transparent.span()[0].static_geometry_sorted);

    const auto all = viewer::area_static_material_draw_batches_for_pass(
        frame, nw::render::RenderPassSelection::all);
    EXPECT_TRUE(all.span().empty());
}

TEST(RenderAreaVisibility, AreaPreparedSurfaceSidecarBridgeCountsDroppedRows)
{
    namespace render = nw::render;
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    std::vector<render::PreparedModelSurfaceDraw> surfaces{
        render::PreparedModelSurfaceDraw{
            .instance_kind = render::ModelInstanceKind::nwn_legacy,
            .payload_kind = render::PreparedModelDrawKind::nwn_legacy,
            .source_draw_index = 0u,
            .material_mode = render::MaterialMode::opaque,
        },
        render::PreparedModelSurfaceDraw{
            .instance_kind = render::ModelInstanceKind::render_model,
            .payload_kind = render::PreparedModelDrawKind::render_model,
            .source_draw_index = 0u,
            .material_mode = render::MaterialMode::opaque,
        },
        render::PreparedModelSurfaceDraw{
            .instance_kind = render::ModelInstanceKind::nwn_legacy,
            .payload_kind = render::PreparedModelDrawKind::nwn_legacy,
            .source_draw_index = 1u,
            .material_mode = render::MaterialMode::opaque,
        },
    };
    std::vector<nwn::PreparedDrawItem> sidecar_storage(1);
    const std::vector<uint32_t> surface_indices{
        0u,
        1u,
        2u,
        std::numeric_limits<uint32_t>::max(),
    };

    std::vector<const nwn::PreparedDrawItem*> sidecar_draws;
    const auto stats = viewer::collect_area_prepared_surface_sidecar_draws(
        sidecar_draws,
        std::span<const uint32_t>{surface_indices.data(), surface_indices.size()},
        std::span<const render::PreparedModelSurfaceDraw>{surfaces.data(), surfaces.size()},
        std::span<const nwn::PreparedDrawItem>{sidecar_storage.data(), sidecar_storage.size()});

    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.input_surface_count, 4u);
    EXPECT_EQ(stats.selected_draw_count, 1u);
    EXPECT_EQ(stats.dropped_non_nwn_surface_count, 1u);
    EXPECT_EQ(stats.dropped_invalid_surface_index_count, 1u);
    EXPECT_EQ(stats.dropped_missing_sidecar_draw_count, 1u);
    EXPECT_EQ(stats.dropped_invalid_sidecar_draw_count, 0u);
    EXPECT_EQ(stats.dropped_surface_count(), 3u);
    ASSERT_EQ(sidecar_draws.size(), 1u);
    EXPECT_EQ(sidecar_draws[0], &sidecar_storage[0]);
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
    expect_static_material_cache_stream_maps_to_draws(area_scene);

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
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    auto disabled = make_area_mesh_model(nw::render::MaterialMode::opaque);
    disabled->render_enabled = false;
    scene.add(std::move(disabled));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
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
    const auto sidecar_draws = area_scene.prepared_draws_for_record(0u);
    ASSERT_EQ(sidecar_draws.size(), 1u);
    const auto cutout_surface_indices = frame.visible_cutout_prepared_surface_indices();
    ASSERT_EQ(cutout_surface_indices.size(), 1u);
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    ASSERT_LT(cutout_surface_indices[0], surfaces.size());
    EXPECT_EQ(surfaces[cutout_surface_indices[0]].source_draw_index, 0u);
    EXPECT_TRUE(frame.visible_opaque_prepared_surface_indices().empty());
}

TEST(RenderAreaVisibility, AreaPreparedSurfacesUseCommonShadowSummary)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    scene.add(make_area_mesh_model(nw::render::MaterialMode::opaque));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };

    auto caster = make_area_mesh_model(nw::render::MaterialMode::opaque);
    auto* caster_mesh = static_cast<nwn::Mesh*>(caster->nodes_.back().get());
    caster->shadow_casters_.push_back(caster_mesh);
    scene.add(std::move(caster));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
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
    model->scene_animation_enabled = true;
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::creature,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.stats().dynamic_record_count, 1u);
    EXPECT_TRUE(area_scene.prepared_draws_for_record(0u).empty());
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());
    EXPECT_EQ(area_scene.prepared_model_draw_list().stats.handle_count, 1u);
    EXPECT_EQ(area_scene.prepared_model_draw_list().stats.nwn_legacy_draw_count, 0u);
}

TEST(RenderAreaVisibility, NonCachedAreaVisibleMaterialListsDropStaleCommonRecords)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 2;
    scene.area_height = 1;

    scene.add(make_area_mesh_model(nw::render::MaterialMode::cutout));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 0,
        .tile_y = 0,
        .static_candidate = true,
    };
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);
    scene.model_instances.destroy(scene.model_instance_handles[0]);

    auto disabled = make_area_mesh_model(nw::render::MaterialMode::opaque);
    disabled->render_enabled = false;
    scene.add(std::move(disabled));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .tile_x = 1,
        .tile_y = 0,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.prepared_draws_for_record(0u).size(), 1u);
    ASSERT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_FALSE(frame.uses_cached_draw_lists());
    EXPECT_EQ(frame.stats().visible_prepared_surface_count, 0u);
    EXPECT_TRUE(frame.visible_cutout_prepared_surface_indices().empty());
    EXPECT_TRUE(frame.visible_opaque_prepared_surface_indices().empty());
}

TEST(RenderAreaVisibility, SortedAreaVisibleMaterialListsUseCommonRecords)
{
    namespace viewer = nw::render::viewer;

    constexpr uint32_t kSortedVisibleSurfaceCount = 4096u;
    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = kSortedVisibleSurfaceCount + 1u;
    scene.area_height = 1;

    for (uint32_t i = 0; i < kSortedVisibleSurfaceCount; ++i) {
        add_static_area_mesh_model(scene, nw::render::MaterialMode::cutout, i);
    }
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, kSortedVisibleSurfaceCount, false);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    expect_static_material_cache_stream_maps_to_draws(area_scene);

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.uses_sorted_static_draw_lists());
    EXPECT_EQ(frame.stats().visible_prepared_surface_count, kSortedVisibleSurfaceCount);
    EXPECT_TRUE(frame.visible_opaque_static_prepared_surface_indices().empty());
    const auto cutout_static_surface_indices = frame.visible_cutout_static_prepared_surface_indices();
    ASSERT_EQ(cutout_static_surface_indices.size(), kSortedVisibleSurfaceCount);

    const auto first_sidecar_draws = area_scene.prepared_draws_for_record(0u);
    ASSERT_EQ(first_sidecar_draws.size(), 1u);

    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto visible_surface_indices = frame.visible_prepared_surface_indices();
    ASSERT_EQ(visible_surface_indices.size(), kSortedVisibleSurfaceCount);
    EXPECT_EQ(frame.visible_opaque_cutout_prepared_surface_indices().size(), kSortedVisibleSurfaceCount);
    EXPECT_TRUE(frame.visible_water_prepared_surface_indices().empty());
    EXPECT_TRUE(frame.visible_transparent_prepared_surface_indices().empty());
    for (const uint32_t surface_index : visible_surface_indices) {
        ASSERT_LT(surface_index, surfaces.size());
        EXPECT_EQ(surfaces[surface_index].material_mode, nw::render::MaterialMode::cutout);
    }
    bool found_first_sidecar_draw = false;
    for (const uint32_t surface_index : cutout_static_surface_indices) {
        ASSERT_LT(surface_index, surfaces.size());
        found_first_sidecar_draw = found_first_sidecar_draw || surfaces[surface_index].source_draw_index == 0u;
    }
    EXPECT_TRUE(found_first_sidecar_draw);
}

TEST(RenderAreaVisibility, SortedAreaVisibleMaterialListsDropStaleCommonRecords)
{
    namespace viewer = nw::render::viewer;

    constexpr uint32_t kSortedVisibleSurfaceCount = 4096u;
    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = kSortedVisibleSurfaceCount + 2u;
    scene.area_height = 1;

    add_static_area_mesh_model(scene, nw::render::MaterialMode::cutout, 0u);
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);
    scene.model_instances.destroy(scene.model_instance_handles[0]);

    for (uint32_t i = 0; i < kSortedVisibleSurfaceCount; ++i) {
        add_static_area_mesh_model(scene, nw::render::MaterialMode::cutout, i + 1u);
    }
    add_static_area_mesh_model(scene, nw::render::MaterialMode::opaque, kSortedVisibleSurfaceCount + 1u, false);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    expect_static_material_cache_stream_maps_to_draws(area_scene);

    const auto stale_sidecar_draws = area_scene.prepared_draws_for_record(0u);
    ASSERT_EQ(stale_sidecar_draws.size(), 1u);
    ASSERT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());

    viewer::AreaRenderFrame frame;
    viewer::AreaRenderCullContext cull{};
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.uses_sorted_static_draw_lists());
    EXPECT_EQ(frame.stats().visible_prepared_surface_count, kSortedVisibleSurfaceCount);
    const auto visible_surface_indices = frame.visible_prepared_surface_indices();
    EXPECT_EQ(visible_surface_indices.size(), kSortedVisibleSurfaceCount);
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    const auto cutout_static_surface_indices = frame.visible_cutout_static_prepared_surface_indices();
    ASSERT_EQ(cutout_static_surface_indices.size(), kSortedVisibleSurfaceCount);
    for (const uint32_t surface_index : cutout_static_surface_indices) {
        ASSERT_LT(surface_index, surfaces.size());
        EXPECT_NE(surfaces[surface_index].source_draw_index, 0u);
    }
}

TEST(RenderAreaVisibility, AreaCommonPreparedDrawsDropStaleHandles)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    scene.add(make_area_mesh_model());
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .static_candidate = true,
    };
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);
    scene.model_instances.destroy(scene.model_instance_handles[0]);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    ASSERT_EQ(area_scene.stats().static_record_count, 1u);
    EXPECT_EQ(area_scene.prepared_draws_for_record(0u).size(), 1u);
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());
    EXPECT_TRUE(area_scene.opaque_static_prepared_surface_indices().empty());
    EXPECT_EQ(area_scene.prepared_model_draw_list().stats.stale_handle_count, 1u);
    EXPECT_EQ(area_scene.prepared_model_draw_list().stats.nwn_legacy_draw_count, 0u);
    EXPECT_TRUE(area_scene.prepared_model_draw_ranges().ranges.empty());
    EXPECT_EQ(area_scene.prepared_model_draw_ranges().stats.empty_range_count, 1u);
}

TEST(RenderAreaVisibility, AreaShadowPreparedDrawsUseCommonRecords)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_area_mesh_model();
    ASSERT_FALSE(model->nodes_.empty());
    auto* shadow_mesh = static_cast<nwn::Mesh*>(model->nodes_.back().get());
    model->shadow_casters_.push_back(shadow_mesh);
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    const auto sidecar_draws = area_scene.prepared_draws_for_record(0u);
    ASSERT_EQ(sidecar_draws.size(), 1u);
    ASSERT_EQ(area_scene.prepared_model_draws_for_record(0u).size(), 1u);
    ASSERT_EQ(area_scene.prepared_model_surface_draws_for_record(0u).size(), 1u);
    const auto shadow_surface_indices = area_scene.shadow_prepared_surface_indices_for_record(0u);
    ASSERT_EQ(shadow_surface_indices.size(), 1u);
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    ASSERT_LT(shadow_surface_indices[0], surfaces.size());
    EXPECT_EQ(surfaces[shadow_surface_indices[0]].source_draw_index, 0u);
    EXPECT_TRUE(surfaces[shadow_surface_indices[0]].casts_shadow);
    const auto sorted_shadow_surface_indices = area_scene.sorted_shadow_prepared_surface_indices();
    ASSERT_EQ(sorted_shadow_surface_indices.size(), 1u);
    EXPECT_EQ(sorted_shadow_surface_indices[0], shadow_surface_indices[0]);
    EXPECT_EQ(area_scene.stats().shadow_prepared_surface_count, 1u);
}

TEST(RenderAreaVisibility, AreaShadowIndirectCacheBridgeCountsDroppedRows)
{
    namespace render = nw::render;
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_area_mesh_model();
    ASSERT_FALSE(model->nodes_.empty());
    auto* shadow_mesh = static_cast<nwn::Mesh*>(model->nodes_.back().get());
    model->shadow_casters_.push_back(shadow_mesh);
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .static_candidate = true,
    };

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    const auto shadow_surface_indices = area_scene.shadow_prepared_surface_indices_for_record(0u);
    ASSERT_EQ(shadow_surface_indices.size(), 1u);
    const auto& surfaces = area_scene.prepared_model_surface_draws().draws;
    ASSERT_LT(shadow_surface_indices[0], surfaces.size());

    viewer::AreaShadowCascadeDraws cascade_draws;
    cascade_draws.append_prepared_surface(shadow_surface_indices[0]);
    cascade_draws.append_prepared_surface(static_cast<uint32_t>(surfaces.size()));

    const auto stats = viewer::refresh_prepared_shadow_indirect_cache(
        cascade_draws,
        std::span<const render::PreparedModelSurfaceDraw>{surfaces.data(), surfaces.size()},
        area_scene.prepared_draws());

    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.input_surface_count, 2u);
    EXPECT_EQ(stats.selected_draw_count, 1u);
    EXPECT_EQ(stats.dropped_non_nwn_surface_count, 0u);
    EXPECT_EQ(stats.dropped_invalid_surface_index_count, 1u);
    EXPECT_EQ(stats.dropped_missing_sidecar_draw_count, 0u);
    EXPECT_EQ(stats.dropped_invalid_sidecar_draw_count, 0u);
    EXPECT_EQ(stats.dropped_surface_count(), 1u);
}

TEST(RenderAreaVisibility, AreaShadowPreparedDrawsDropStaleCommonRecords)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = make_area_mesh_model();
    ASSERT_FALSE(model->nodes_.empty());
    auto* shadow_mesh = static_cast<nwn::Mesh*>(model->nodes_.back().get());
    model->shadow_casters_.push_back(shadow_mesh);
    scene.add(std::move(model));
    scene.area_model_info.back() = viewer::AreaRenderSourceInfo{
        .kind = viewer::AreaRenderRecordKind::tile,
        .static_candidate = true,
    };
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);
    scene.model_instances.destroy(scene.model_instance_handles[0]);

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);

    EXPECT_EQ(area_scene.prepared_draws_for_record(0u).size(), 1u);
    EXPECT_TRUE(area_scene.prepared_model_draws_for_record(0u).empty());
    EXPECT_TRUE(area_scene.prepared_model_surface_draws_for_record(0u).empty());
    EXPECT_TRUE(area_scene.shadow_prepared_surface_indices_for_record(0u).empty());
    EXPECT_TRUE(area_scene.sorted_shadow_prepared_surface_indices().empty());
    EXPECT_EQ(area_scene.stats().shadow_prepared_surface_count, 0u);
}

TEST(RenderAreaVisibility, StaleCommonInstanceHandleDropsAreaRecord)
{
    namespace viewer = nw::render::viewer;
    namespace nwn = nw::render::nwn;

    viewer::PreviewScene scene;
    scene.is_area = true;
    scene.area_width = 1;
    scene.area_height = 1;

    auto model = std::make_unique<nwn::ModelInstance>();
    model->bounds = nw::render::Bounds{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 1.0f, 1.0f}};
    model->material_pass_mask = 0x1u;
    model->shadow_casters_.push_back(nullptr);
    scene.add(std::move(model));

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    ASSERT_EQ(area_scene.model_instance_handles().size(), 1u);

    scene.model_instances.destroy(scene.model_instance_handles[0]);
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
    particle_system.owner = scene.models[far_model_index].get();
    particle_system.owner_model_index = static_cast<uint32_t>(far_model_index);

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
    particle_system.owner_kind = nw::render::ModelInstanceKind::render_model;
    particle_system.owner_model_index = 0u;
    particle_system.owner_instance_handle = scene.static_model_instance_handles[0];

    viewer::AreaRenderScene area_scene;
    area_scene.rebuild(scene);
    const uint32_t legacy_record = area_scene.record_index_for_model(0u);
    const uint32_t render_model_record = area_scene.record_index_for_render_model(0u);
    ASSERT_NE(legacy_record, viewer::kInvalidAreaRenderRecordIndex);
    ASSERT_NE(render_model_record, viewer::kInvalidAreaRenderRecordIndex);

    viewer::AreaRenderFrame frame;
    const uint8_t visible_chunks[] = {1u, 0u};
    viewer::AreaRenderCullContext cull{};
    cull.enabled = true;
    cull.chunk_visibility_enabled = true;
    cull.visible_chunk_mask = std::span<const uint8_t>{visible_chunks};
    cull.view_projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    prepare_area_frame(area_scene, frame, cull);

    ASSERT_TRUE(frame.record_visible(legacy_record));
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
