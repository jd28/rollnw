#include <gtest/gtest.h>

#include <nw/render/model_renderer.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/viewer/preview_model_draws.hpp>
#include <nw/render/viewer/preview_scene.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace {

std::unique_ptr<nw::render::RenderModel> make_two_primitive_render_model()
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->materials.push_back(nw::render::Material{
        .alpha_mode = nw::render::MaterialMode::opaque,
    });
    model->materials.push_back(nw::render::Material{
        .material_uses_fallback = true,
        .alpha_mode = nw::render::MaterialMode::cutout,
    });
    model->primitives.push_back(nw::render::Primitive{
        .vertices = {},
        .indices = {},
        .vertex_count = 3,
        .index_count = 3,
        .material = 0,
        .transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}),
        .bounds = {
            .min = {0.0f, 0.0f, 0.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
    });
    model->primitives.push_back(nw::render::Primitive{
        .vertices = {},
        .indices = {},
        .vertex_count = 4,
        .index_count = 6,
        .material = 1,
        .transform = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 3.0f, 0.0f}),
        .bounds = {
            .min = {0.0f, 0.0f, 0.0f},
            .max = {2.0f, 2.0f, 2.0f},
        },
    });
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {2.0f, 3.0f, 2.0f},
    };
    return model;
}

std::unique_ptr<nw::render::RenderModel> make_skinned_render_model()
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->materials.push_back(nw::render::Material{
        .alpha_mode = nw::render::MaterialMode::opaque,
    });
    model->nodes.push_back(nw::render::Node{});
    model->nodes.push_back(nw::render::Node{});
    model->skins.push_back(nw::render::Skin{
        .joints = {1},
        .inverse_bind_matrices = {glm::mat4{1.0f}},
    });
    model->primitives.push_back(nw::render::Primitive{
        .vertices = {},
        .indices = {},
        .vertex_count = 3,
        .index_count = 3,
        .material = 0,
        .skin = 0,
        .node = 0,
        .skinned = true,
        .bounds = {
            .min = {0.0f, 0.0f, 0.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
    });
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    return model;
}

std::span<const nw::render::ModelInstanceHandle> handle_span(
    const std::vector<nw::render::ModelInstanceHandle>& handles)
{
    return {handles.data(), handles.size()};
}

} // namespace

TEST(RenderPreparedModelDraws, CollectsRenderModelPrimitiveDraws)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_primitive_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    auto* instance = scene.model_instances.get(scene.static_model_instance_handles[0]);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(prepared.common.instance_offsets.size(), 2u);
    EXPECT_EQ(prepared.common.instance_offsets[0], 0u);
    EXPECT_EQ(prepared.common.instance_offsets[1], 2u);
    ASSERT_EQ(prepared.common.draws.size(), 2u);
    EXPECT_EQ(prepared.common.stats.handle_count, 1u);
    EXPECT_EQ(prepared.common.stats.visible_instance_count, 1u);
    EXPECT_EQ(prepared.common.stats.render_model_instance_count, 1u);
    EXPECT_EQ(prepared.common.stats.render_model_draw_count, 2u);
    EXPECT_EQ(prepared.common.stats.material_fallback_draw_count, 1u);
    EXPECT_EQ(prepared.common.stats.render_model_material_fallback_draw_count, 1u);
    EXPECT_EQ(prepared.common.stats.nwn_legacy_material_fallback_draw_count, 0u);
    EXPECT_EQ(prepared.common.stats.invalid_draw_count, 0u);

    const auto draw_span = nw::render::prepared_model_draws_for_handle_index(prepared.common, 0);
    ASSERT_EQ(draw_span.size(), 2u);
    ASSERT_EQ(prepared.ranges.ranges.size(), 1u);
    EXPECT_EQ(prepared.ranges.stats.handle_count, 1u);
    EXPECT_EQ(prepared.ranges.stats.range_count, 1u);
    EXPECT_EQ(prepared.ranges.stats.draw_count, 2u);
    EXPECT_EQ(prepared.ranges.ranges[0].handle_index, 0u);
    EXPECT_EQ(prepared.ranges.ranges[0].draw_begin, 0u);
    EXPECT_EQ(prepared.ranges.ranges[0].draw_count, 2u);
    EXPECT_EQ(prepared.ranges.ranges[0].instance_source_index, 0u);
    const auto range_span = nw::render::prepared_model_draws_for_range(prepared.common, prepared.ranges.ranges[0]);
    ASSERT_EQ(range_span.size(), 2u);

    const auto& first = prepared.common.draws[0];
    EXPECT_EQ(first.kind, nw::render::PreparedModelDrawKind::render_model);
    EXPECT_EQ(first.instance_source_index, 0u);
    EXPECT_EQ(first.source_draw_index, 0u);
    EXPECT_EQ(first.material_index, 0u);
    EXPECT_EQ(first.material_mode, nw::render::MaterialMode::opaque);
    EXPECT_FALSE(first.material_uses_fallback);
    EXPECT_EQ(first.material_payload, nw::render::PreparedModelMaterialPayloadKind::pbr);
    EXPECT_FALSE(first.skinned);
    EXPECT_NEAR(first.world[3].x, 12.0f, 1.0e-5f);
    EXPECT_NEAR(first.bounds.min.x, 12.0f, 1.0e-5f);

    const auto& second = prepared.common.draws[1];
    EXPECT_EQ(second.source_draw_index, 1u);
    EXPECT_EQ(second.material_index, 1u);
    EXPECT_EQ(second.material_mode, nw::render::MaterialMode::cutout);
    EXPECT_TRUE(second.material_uses_fallback);
    EXPECT_EQ(second.material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);
    EXPECT_NEAR(second.world[3].x, 10.0f, 1.0e-5f);
    EXPECT_NEAR(second.world[3].y, 3.0f, 1.0e-5f);

    const auto validation = viewer::validate_prepared_model_draws(
        scene,
        prepared,
        handle_span(scene.static_model_instance_handles));
    EXPECT_TRUE(validation.valid());
    EXPECT_EQ(validation.expected_stats.render_model_draw_count, 2u);
    EXPECT_EQ(validation.expected_stats.material_fallback_draw_count, 1u);
    EXPECT_EQ(validation.expected_stats.render_model_material_fallback_draw_count, 1u);
    EXPECT_EQ(validation.expected_stats.nwn_legacy_material_fallback_draw_count, 0u);
    EXPECT_EQ(validation.prepared_stats.material_fallback_draw_count, 1u);
    EXPECT_EQ(validation.prepared_stats.render_model_material_fallback_draw_count, 1u);
    EXPECT_EQ(validation.prepared_stats.nwn_legacy_material_fallback_draw_count, 0u);
    EXPECT_EQ(validation.prepared_draw_count, 2u);
    EXPECT_EQ(validation.expected_materials.opaque_count, 1u);
    EXPECT_EQ(validation.expected_materials.cutout_count, 1u);
    EXPECT_EQ(validation.prepared_materials.opaque_count, 1u);
    EXPECT_EQ(validation.prepared_materials.cutout_count, 1u);
}

TEST(RenderPreparedModelDraws, RenderModelPrimitiveDrawsUseSampledNodeTransforms)
{
    namespace viewer = nw::render::viewer;

    auto model = make_two_primitive_render_model();
    model->nodes.push_back(nw::render::Node{});
    model->primitives[0].node = 0;

    viewer::PreviewScene scene;
    scene.add(std::move(model));
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    auto* instance = scene.model_instances.get(scene.static_model_instance_handles[0]);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    instance->attachment_node_world_transforms.resize(1);
    instance->attachment_node_transform_valid.assign(1, 0u);
    instance->attachment_node_world_transforms[0] = glm::translate(
        glm::mat4{1.0f},
        glm::vec3{42.0f, 5.0f, 0.0f});
    instance->attachment_node_transform_valid[0] = 1u;

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(prepared.common.draws.size(), 2u);
    const auto& animated = prepared.common.draws[0];
    EXPECT_NEAR(animated.world[3].x, 42.0f, 1.0e-5f);
    EXPECT_NEAR(animated.world[3].y, 5.0f, 1.0e-5f);
    EXPECT_NEAR(animated.bounds.min.x, 42.0f, 1.0e-5f);
    EXPECT_NEAR(animated.bounds.min.y, 5.0f, 1.0e-5f);

    const auto& fallback = prepared.common.draws[1];
    EXPECT_NEAR(fallback.world[3].x, 10.0f, 1.0e-5f);
    EXPECT_NEAR(fallback.world[3].y, 3.0f, 1.0e-5f);
}

TEST(RenderPreparedModelDraws, RenderModelMaterialOverridesUseCommonHandles)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_primitive_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    auto* instance = scene.model_instances.get(scene.static_model_instance_handles[0]);
    ASSERT_NE(instance, nullptr);

    nw::render::ModelMaterialOverride material_override;
    material_override.material.alpha_mode = nw::render::MaterialMode::opaque;
    material_override.material.material_uses_fallback = false;
    const auto override_handle = scene.material_overrides.create(material_override);

    instance->material_override_handles.resize(2);
    instance->material_override_handles[1] = override_handle;

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(prepared.common.draws.size(), 2u);
    EXPECT_FALSE(prepared.common.draws[0].material_override.valid());
    EXPECT_EQ(prepared.common.draws[1].material_override, override_handle);
    EXPECT_EQ(prepared.common.draws[1].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_FALSE(prepared.common.draws[1].material_uses_fallback);
    EXPECT_EQ(prepared.common.draws[1].material_payload, nw::render::PreparedModelMaterialPayloadKind::pbr);
    EXPECT_EQ(prepared.common.stats.material_override_draw_count, 1u);
    EXPECT_EQ(prepared.common.stats.invalid_material_override_handle_count, 0u);

    const auto validation = viewer::validate_prepared_model_draws(
        scene,
        prepared,
        handle_span(scene.static_model_instance_handles));
    EXPECT_TRUE(validation.valid());
    EXPECT_EQ(validation.expected_stats.material_override_draw_count, 1u);
    EXPECT_EQ(validation.prepared_materials.opaque_count, 2u);
    EXPECT_EQ(validation.prepared_materials.cutout_count, 0u);

    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(
        prepared,
        surfaces,
        scene,
        handle_span(scene.static_model_instance_handles));
    const nw::render::PreparedModelSurfaceDraw* override_surface = nullptr;
    for (const auto& surface : surfaces.draws) {
        if (surface.source_draw_index == 1u) {
            override_surface = &surface;
            break;
        }
    }
    ASSERT_NE(override_surface, nullptr);
    EXPECT_EQ(override_surface->material_override, override_handle);

    const auto binding_stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        prepared.common,
        prepared.ranges);
    EXPECT_TRUE(binding_stats.valid());
    EXPECT_EQ(binding_stats.material_override_surface_count, 1u);

    nw::render::PreparedRenderModelSurfacePacketList packets;
    nw::render::collect_prepared_render_model_surface_packets(
        packets,
        *scene.static_models[0],
        std::span<const nw::render::PreparedModelSurfaceDraw>{override_surface, 1u},
        nw::render::RenderPassSelection::all,
        nullptr,
        &scene.material_overrides);
    EXPECT_TRUE(packets.stats.valid());
    EXPECT_EQ(packets.stats.valid_surface_count, 1u);

    nw::render::collect_prepared_render_model_surface_packets(
        packets,
        *scene.static_models[0],
        std::span<const nw::render::PreparedModelSurfaceDraw>{override_surface, 1u},
        nw::render::RenderPassSelection::all);
    EXPECT_FALSE(packets.stats.valid());
    EXPECT_EQ(packets.stats.invalid_surface_count, 1u);
    EXPECT_EQ(packets.stats.invalid_material_override_handle_count, 1u);

    scene.material_overrides.destroy(override_handle);
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(prepared.common.draws.size(), 2u);
    EXPECT_FALSE(prepared.common.draws[1].material_override.valid());
    EXPECT_EQ(prepared.common.draws[1].material_mode, nw::render::MaterialMode::cutout);
    EXPECT_TRUE(prepared.common.draws[1].material_uses_fallback);
    EXPECT_EQ(prepared.common.stats.material_override_draw_count, 0u);
    EXPECT_EQ(prepared.common.stats.invalid_material_override_handle_count, 1u);
}

TEST(RenderPreparedModelDraws, RenderModelPltMaterialOverridesUseCommonHandles)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_primitive_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    auto* instance = scene.model_instances.get(scene.static_model_instance_handles[0]);
    ASSERT_NE(instance, nullptr);
    ASSERT_EQ(scene.static_models.size(), 1u);
    ASSERT_TRUE(scene.static_models[0]);

    auto override_material = scene.static_models[0]->materials[0];
    override_material.albedo_index = 9u;
    override_material.albedo_uses_plt = true;
    override_material.plt_enabled = true;
    override_material.plt_colors0 = glm::uvec4{1u, 2u, 3u, 4u};
    override_material.plt_colors1 = glm::uvec4{5u, 6u, 7u, 8u};
    override_material.plt_colors2 = glm::uvec4{9u, 10u, 0u, 0u};
    const auto override_handle = scene.material_overrides.create(nw::render::ModelMaterialOverride{
        .material = override_material,
    });

    instance->material_override_handles.resize(1);
    instance->material_override_handles[0] = override_handle;

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(prepared.common.draws.size(), 2u);
    EXPECT_EQ(prepared.common.draws[0].material_override, override_handle);
    EXPECT_EQ(prepared.common.draws[0].material_payload, nw::render::PreparedModelMaterialPayloadKind::nwn_legacy_plt);
    EXPECT_EQ(prepared.common.stats.material_override_draw_count, 1u);
    EXPECT_EQ(prepared.common.stats.invalid_material_override_handle_count, 0u);

    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(
        prepared,
        surfaces,
        scene,
        handle_span(scene.static_model_instance_handles));
    const nw::render::PreparedModelSurfaceDraw* override_surface = nullptr;
    for (const auto& surface : surfaces.draws) {
        if (surface.source_draw_index == 0u) {
            override_surface = &surface;
            break;
        }
    }
    ASSERT_NE(override_surface, nullptr);
    EXPECT_EQ(override_surface->material_override, override_handle);
    EXPECT_EQ(override_surface->material_payload, nw::render::PreparedModelMaterialPayloadKind::nwn_legacy_plt);

    nw::render::PreparedRenderModelSurfacePacketList packets;
    nw::render::collect_prepared_render_model_surface_packets(
        packets,
        *scene.static_models[0],
        std::span<const nw::render::PreparedModelSurfaceDraw>{override_surface, 1u},
        nw::render::RenderPassSelection::all,
        nullptr,
        &scene.material_overrides);
    EXPECT_TRUE(packets.stats.valid());
    EXPECT_EQ(packets.stats.valid_surface_count, 1u);

    scene.material_overrides.destroy(override_handle);
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(prepared.common.draws.size(), 2u);
    EXPECT_FALSE(prepared.common.draws[0].material_override.valid());
    EXPECT_EQ(prepared.common.draws[0].material_payload, nw::render::PreparedModelMaterialPayloadKind::pbr);
    EXPECT_EQ(prepared.common.stats.invalid_material_override_handle_count, 1u);
}

TEST(RenderPreparedModelDraws, StaticHandleBatchExcludesLegacySceneHandles)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto legacy_model = std::make_unique<nwn::ModelInstance>();
    legacy_model->render_enabled = true;
    scene.add(std::move(legacy_model));
    scene.add(make_two_primitive_render_model());
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));

    EXPECT_EQ(prepared.common.stats.handle_count, 1u);
    EXPECT_EQ(prepared.common.stats.nwn_legacy_instance_count, 0u);
    EXPECT_EQ(prepared.common.stats.nwn_legacy_draw_count, 0u);
    EXPECT_EQ(prepared.common.stats.render_model_instance_count, 1u);
    EXPECT_EQ(prepared.common.stats.render_model_draw_count, 2u);
    ASSERT_EQ(prepared.ranges.ranges.size(), 1u);
    EXPECT_EQ(prepared.ranges.ranges[0].kind, nw::render::PreparedModelDrawKind::render_model);

    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, prepared.common, prepared.ranges);

    EXPECT_TRUE(surfaces.stats.valid());
    EXPECT_EQ(surfaces.stats.nwn_legacy_draw_count, 0u);
    EXPECT_EQ(surfaces.stats.render_model_draw_count, 2u);
    ASSERT_EQ(surfaces.draws.size(), 2u);
    EXPECT_EQ(surfaces.draws[0].payload_kind, nw::render::PreparedModelDrawKind::render_model);
    EXPECT_EQ(surfaces.draws[1].payload_kind, nw::render::PreparedModelDrawKind::render_model);
}

TEST(RenderPreparedModelDraws, RenderModelSurfaceParityIncludesFallbackState)
{
    const auto model = make_two_primitive_render_model();

    nw::render::PreparedModelSurfaceDraw surface;
    surface.instance_kind = nw::render::ModelInstanceKind::render_model;
    surface.payload_kind = nw::render::PreparedModelDrawKind::render_model;
    surface.instance_source_index = 0u;
    surface.source_draw_index = 1u;
    surface.material_index = 1u;
    surface.skin_index = nw::render::kInvalidPreparedModelDrawIndex;
    surface.material_mode = nw::render::MaterialMode::cutout;
    surface.material_uses_fallback = true;
    surface.material_payload = nw::render::PreparedModelMaterialPayloadKind::fallback;
    surface.skinned = false;
    surface.casts_shadow = true;

    EXPECT_TRUE(nw::render::prepared_render_model_surface_matches_model(*model, surface));
    EXPECT_TRUE(nw::render::prepared_render_model_shadow_surface_matches_model(*model, surface));

    auto stale_fallback = surface;
    stale_fallback.material_uses_fallback = false;
    EXPECT_FALSE(nw::render::prepared_render_model_surface_matches_model(*model, stale_fallback));
    EXPECT_FALSE(nw::render::prepared_render_model_shadow_surface_matches_model(*model, stale_fallback));

    auto stale_material_payload = surface;
    stale_material_payload.material_payload = nw::render::PreparedModelMaterialPayloadKind::pbr;
    EXPECT_FALSE(nw::render::prepared_render_model_surface_matches_model(*model, stale_material_payload));
    EXPECT_FALSE(nw::render::prepared_render_model_shadow_surface_matches_model(*model, stale_material_payload));

    auto stale_material = surface;
    stale_material.material_mode = nw::render::MaterialMode::opaque;
    EXPECT_FALSE(nw::render::prepared_render_model_surface_matches_model(*model, stale_material));
    EXPECT_FALSE(nw::render::prepared_render_model_shadow_surface_matches_model(*model, stale_material));

    auto non_shadow = surface;
    non_shadow.casts_shadow = false;
    EXPECT_TRUE(nw::render::prepared_render_model_surface_matches_model(*model, non_shadow));
    EXPECT_FALSE(nw::render::prepared_render_model_shadow_surface_matches_model(*model, non_shadow));

    auto wrong_payload = surface;
    wrong_payload.payload_kind = nw::render::PreparedModelDrawKind::nwn_legacy;
    EXPECT_FALSE(nw::render::prepared_render_model_surface_matches_model(*model, wrong_payload));
    EXPECT_FALSE(nw::render::prepared_render_model_shadow_surface_matches_model(*model, wrong_payload));
}

TEST(RenderPreparedModelDraws, PreparedRenderModelSurfacePacketsValidateAssetPayloads)
{
    const auto model = make_two_primitive_render_model();

    nw::render::PreparedModelSurfaceDraw opaque;
    opaque.instance_kind = nw::render::ModelInstanceKind::render_model;
    opaque.payload_kind = nw::render::PreparedModelDrawKind::render_model;
    opaque.instance_source_index = 0u;
    opaque.source_draw_index = 0u;
    opaque.material_index = 0u;
    opaque.skin_index = nw::render::kInvalidPreparedModelDrawIndex;
    opaque.material_mode = nw::render::MaterialMode::opaque;
    opaque.skinned = false;

    nw::render::PreparedModelSurfaceDraw cutout = opaque;
    cutout.source_draw_index = 1u;
    cutout.material_index = 1u;
    cutout.material_mode = nw::render::MaterialMode::cutout;
    cutout.material_uses_fallback = true;
    cutout.material_payload = nw::render::PreparedModelMaterialPayloadKind::fallback;

    auto primitive_mismatch = cutout;
    primitive_mismatch.material_index = 0u;

    auto material_mismatch = cutout;
    material_mismatch.material_uses_fallback = false;

    auto invalid_source_draw = opaque;
    invalid_source_draw.source_draw_index = 9u;

    auto invalid_source = opaque;
    invalid_source.instance_source_index = nw::render::kInvalidPreparedModelDrawIndex;

    auto non_render_model = opaque;
    non_render_model.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
    non_render_model.payload_kind = nw::render::PreparedModelDrawKind::nwn_legacy;

    auto filtered_transparent = opaque;
    filtered_transparent.material_mode = nw::render::MaterialMode::transparent;

    std::array surfaces{
        opaque,
        cutout,
        primitive_mismatch,
        material_mismatch,
        invalid_source_draw,
        invalid_source,
        non_render_model,
        filtered_transparent,
    };

    nw::render::PreparedRenderModelSurfacePacketList packets;
    nw::render::collect_prepared_render_model_surface_packets(
        packets,
        *model,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.data(), surfaces.size()},
        nw::render::RenderPassSelection::opaque_cutout);

    EXPECT_FALSE(packets.stats.valid());
    EXPECT_EQ(packets.stats.input_surface_count, 8u);
    EXPECT_EQ(packets.stats.pass_filtered_surface_count, 1u);
    EXPECT_EQ(packets.stats.candidate_surface_count, 7u);
    EXPECT_EQ(packets.stats.valid_surface_count, 2u);
    EXPECT_EQ(packets.stats.invalid_surface_count, 5u);
    EXPECT_EQ(packets.stats.non_render_model_surface_count, 1u);
    EXPECT_EQ(packets.stats.invalid_source_index_count, 1u);
    EXPECT_EQ(packets.stats.invalid_source_draw_index_count, 1u);
    EXPECT_EQ(packets.stats.invalid_material_index_count, 0u);
    EXPECT_EQ(packets.stats.primitive_mismatch_count, 1u);
    EXPECT_EQ(packets.stats.material_mismatch_count, 1u);

    ASSERT_EQ(packets.surface_indices.size(), 2u);
    EXPECT_EQ(packets.surface_indices[0], 0u);
    EXPECT_EQ(packets.surface_indices[1], 1u);
}

TEST(RenderPreparedModelDraws, PreparedRenderModelSurfacePacketsCountSkinBindingFallbacks)
{
    const auto model = make_skinned_render_model();

    nw::render::PreparedModelSurfaceDraw table_bound;
    table_bound.instance_kind = nw::render::ModelInstanceKind::render_model;
    table_bound.payload_kind = nw::render::PreparedModelDrawKind::render_model;
    table_bound.instance_source_index = 0u;
    table_bound.source_draw_index = 0u;
    table_bound.material_index = 0u;
    table_bound.skin_index = 0u;
    table_bound.skin_table_index = 0u;
    table_bound.material_mode = nw::render::MaterialMode::opaque;
    table_bound.skinned = true;

    auto bind_pose = table_bound;
    bind_pose.skin_table_index = nw::render::kInvalidPreparedModelDrawIndex;

    auto invalid_index = table_bound;
    invalid_index.skin_table_index = 9u;

    auto invalid_range = table_bound;
    invalid_range.skin_table_index = 1u;

    nw::render::PreparedRenderModelSkinTable skin_table;
    skin_table.matrices.push_back(glm::mat4{1.0f});
    skin_table.ranges.push_back(nw::render::PreparedRenderModelSkinRange{
        .instance = {},
        .source_skin_index = 0u,
        .matrix_begin = 0u,
        .matrix_count = 1u,
    });
    skin_table.ranges.push_back(nw::render::PreparedRenderModelSkinRange{
        .instance = {},
        .source_skin_index = 0u,
        .matrix_begin = 0u,
        .matrix_count = 4u,
    });

    const std::array surfaces{
        table_bound,
        bind_pose,
        invalid_index,
        invalid_range,
    };

    nw::render::PreparedRenderModelSurfacePacketList packets;
    nw::render::collect_prepared_render_model_surface_packets(
        packets,
        *model,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.data(), surfaces.size()},
        nw::render::RenderPassSelection::opaque_cutout,
        &skin_table);

    EXPECT_FALSE(packets.stats.valid());
    EXPECT_EQ(packets.stats.invalid_surface_count, 0u);
    EXPECT_EQ(packets.stats.valid_surface_count, 4u);
    EXPECT_EQ(packets.surface_indices.size(), 4u);
    EXPECT_EQ(packets.stats.skin_bindings.skinned_surface_count, 4u);
    EXPECT_EQ(packets.stats.skin_bindings.table_bound_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.bind_pose_fallback_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.missing_table_surface_count, 0u);
    EXPECT_EQ(packets.stats.skin_bindings.invalid_table_index_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.invalid_table_range_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.fallback_surface_count(), 3u);

    nw::render::collect_prepared_render_model_surface_packets(
        packets,
        *model,
        std::span<const nw::render::PreparedModelSurfaceDraw>{&table_bound, 1u},
        nw::render::RenderPassSelection::opaque_cutout);

    EXPECT_FALSE(packets.stats.valid());
    EXPECT_EQ(packets.stats.invalid_surface_count, 0u);
    EXPECT_EQ(packets.stats.valid_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.skinned_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.table_bound_surface_count, 0u);
    EXPECT_EQ(packets.stats.skin_bindings.missing_table_surface_count, 1u);
    EXPECT_EQ(packets.stats.skin_bindings.fallback_surface_count(), 1u);

    nw::render::PreparedRenderModelSurfaceSubmissionStats submission_stats;
    nw::render::render_prepared_render_model_surfaces(
        nw::render::ModelRenderContext{},
        nullptr,
        *model,
        std::span<const nw::render::PreparedModelSurfaceDraw>{&table_bound, 1u},
        nw::render::RenderContext{},
        nw::render::RenderPassSelection::opaque_cutout,
        nullptr,
        nullptr,
        &submission_stats);

    EXPECT_FALSE(submission_stats.valid());
    EXPECT_EQ(submission_stats.submitted_surface_count, 0u);
    EXPECT_EQ(submission_stats.dropped_invalid_surface_count, 0u);
    EXPECT_EQ(submission_stats.skin_bindings.skinned_surface_count, 1u);
    EXPECT_EQ(submission_stats.skin_bindings.missing_table_surface_count, 1u);
}

TEST(RenderPreparedModelDraws, PreparedRenderModelSkinTablesAssignInstanceIndexedRanges)
{
    nw::render::ModelInstanceStore instances;

    nw::render::ModelInstance animated;
    animated.kind = nw::render::ModelInstanceKind::render_model;
    animated.animation.skin_matrices.resize(2);
    animated.animation.skin_matrices[0].push_back(
        glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}));
    animated.animation.skin_matrices[0].push_back(
        glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    animated.animation.skin_matrices[1].push_back(
        glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 3.0f, 0.0f}));
    const auto animated_handle = instances.create(std::move(animated));

    nw::render::ModelInstance bind_pose_fallback;
    bind_pose_fallback.kind = nw::render::ModelInstanceKind::render_model;
    const auto fallback_handle = instances.create(std::move(bind_pose_fallback));

    nw::render::ModelInstance stale_instance;
    stale_instance.kind = nw::render::ModelInstanceKind::render_model;
    const auto stale_handle = instances.create(std::move(stale_instance));
    instances.destroy(stale_handle);

    const auto make_surface = [](nw::render::ModelInstanceHandle instance,
                                  nw::render::ModelInstanceKind instance_kind,
                                  nw::render::PreparedModelDrawKind payload_kind,
                                  bool skinned,
                                  uint32_t skin_index) {
        nw::render::PreparedModelSurfaceDraw surface;
        surface.instance = instance;
        surface.instance_kind = instance_kind;
        surface.payload_kind = payload_kind;
        surface.skinned = skinned;
        surface.skin_index = skin_index;
        surface.skin_table_index = 99u;
        return surface;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> surfaces{
        make_surface(
            animated_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            true,
            0u),
        make_surface(
            animated_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            true,
            0u),
        make_surface(
            animated_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            true,
            1u),
        make_surface(
            animated_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            false,
            nw::render::kInvalidPreparedModelDrawIndex),
        make_surface(
            animated_handle,
            nw::render::ModelInstanceKind::nwn_legacy,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            true,
            0u),
        make_surface(
            stale_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            true,
            0u),
        make_surface(
            fallback_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            true,
            0u),
        make_surface(
            animated_handle,
            nw::render::ModelInstanceKind::render_model,
            nw::render::PreparedModelDrawKind::render_model,
            true,
            nw::render::kInvalidPreparedModelDrawIndex),
    };

    nw::render::PreparedRenderModelSkinTable skin_table;
    nw::render::collect_prepared_render_model_skin_tables(
        skin_table,
        std::span<nw::render::PreparedModelSurfaceDraw>{surfaces.data(), surfaces.size()},
        instances);

    EXPECT_FALSE(skin_table.stats.valid());
    EXPECT_EQ(skin_table.stats.input_surface_count, 8u);
    EXPECT_EQ(skin_table.stats.render_model_skinned_surface_count, 6u);
    EXPECT_EQ(skin_table.stats.assigned_surface_count, 3u);
    EXPECT_EQ(skin_table.stats.reused_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.bind_pose_fallback_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.unskinned_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.non_render_model_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.stale_instance_count, 1u);
    EXPECT_EQ(skin_table.stats.invalid_skin_index_count, 1u);
    EXPECT_EQ(skin_table.stats.invalid_matrix_range_count, 0u);
    EXPECT_EQ(skin_table.stats.table_entry_count, 2u);
    EXPECT_EQ(skin_table.stats.matrix_count, 3u);

    ASSERT_EQ(skin_table.ranges.size(), 2u);
    EXPECT_EQ(skin_table.ranges[0].instance, animated_handle);
    EXPECT_EQ(skin_table.ranges[0].source_skin_index, 0u);
    EXPECT_EQ(skin_table.ranges[0].matrix_begin, 0u);
    EXPECT_EQ(skin_table.ranges[0].matrix_count, 2u);
    EXPECT_EQ(skin_table.ranges[1].instance, animated_handle);
    EXPECT_EQ(skin_table.ranges[1].source_skin_index, 1u);
    EXPECT_EQ(skin_table.ranges[1].matrix_begin, 2u);
    EXPECT_EQ(skin_table.ranges[1].matrix_count, 1u);

    ASSERT_EQ(skin_table.matrices.size(), 3u);
    EXPECT_NEAR(skin_table.matrices[0][3].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(skin_table.matrices[1][3].x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(skin_table.matrices[2][3].y, 3.0f, 1.0e-5f);

    EXPECT_EQ(surfaces[0].skin_table_index, 0u);
    EXPECT_EQ(surfaces[1].skin_table_index, 0u);
    EXPECT_EQ(surfaces[2].skin_table_index, 1u);
    EXPECT_EQ(surfaces[3].skin_table_index, nw::render::kInvalidPreparedModelDrawIndex);
    EXPECT_EQ(surfaces[4].skin_table_index, nw::render::kInvalidPreparedModelDrawIndex);
    EXPECT_EQ(surfaces[5].skin_table_index, nw::render::kInvalidPreparedModelDrawIndex);
    EXPECT_EQ(surfaces[6].skin_table_index, nw::render::kInvalidPreparedModelDrawIndex);
    EXPECT_EQ(surfaces[7].skin_table_index, nw::render::kInvalidPreparedModelDrawIndex);
}

TEST(RenderPreparedModelDraws, PreparedRenderModelSkinTablesRejectOverLimitMatrixRanges)
{
    nw::render::ModelInstanceStore instances;

    nw::render::ModelInstance animated;
    animated.kind = nw::render::ModelInstanceKind::render_model;
    animated.animation.skin_matrices.resize(1);
    animated.animation.skin_matrices[0].resize(nw::render::kModelMaxSkinBones + 1, glm::mat4{1.0f});
    const auto animated_handle = instances.create(std::move(animated));

    nw::render::PreparedModelSurfaceDraw surface;
    surface.instance = animated_handle;
    surface.instance_kind = nw::render::ModelInstanceKind::render_model;
    surface.payload_kind = nw::render::PreparedModelDrawKind::render_model;
    surface.skinned = true;
    surface.skin_index = 0u;
    surface.skin_table_index = 99u;

    std::vector<nw::render::PreparedModelSurfaceDraw> surfaces{surface};
    nw::render::PreparedRenderModelSkinTable skin_table;
    nw::render::collect_prepared_render_model_skin_tables(
        skin_table,
        std::span<nw::render::PreparedModelSurfaceDraw>{surfaces.data(), surfaces.size()},
        instances);

    EXPECT_FALSE(skin_table.stats.valid());
    EXPECT_EQ(skin_table.stats.input_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.render_model_skinned_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.assigned_surface_count, 0u);
    EXPECT_EQ(skin_table.stats.invalid_matrix_range_count, 1u);
    EXPECT_TRUE(skin_table.ranges.empty());
    EXPECT_TRUE(skin_table.matrices.empty());
    EXPECT_EQ(surfaces[0].skin_table_index, nw::render::kInvalidPreparedModelDrawIndex);
}

TEST(RenderPreparedModelDraws, ViewerSurfaceCollectionOwnsRenderModelSkinTable)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_skinned_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    const auto handle = scene.static_model_instance_handles[0];
    auto* instance = scene.model_instances.get(handle);
    ASSERT_NE(instance, nullptr);
    instance->animation.skin_matrices.resize(1);
    instance->animation.skin_matrices[0].push_back(
        glm::translate(glm::mat4{1.0f}, glm::vec3{4.0f, 0.0f, 0.0f}));

    viewer::PreviewPreparedModelDraws prepared;
    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(
        prepared,
        surfaces,
        scene,
        handle_span(scene.static_model_instance_handles));

    ASSERT_EQ(surfaces.draws.size(), 1u);
    EXPECT_EQ(surfaces.draws[0].instance, handle);
    EXPECT_TRUE(surfaces.draws[0].skinned);
    EXPECT_EQ(surfaces.draws[0].skin_index, 0u);
    EXPECT_EQ(surfaces.draws[0].skin_table_index, 0u);

    const auto& skin_table = surfaces.render_model_skins;
    EXPECT_TRUE(skin_table.stats.valid());
    EXPECT_EQ(skin_table.stats.input_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.render_model_skinned_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.assigned_surface_count, 1u);
    EXPECT_EQ(skin_table.stats.table_entry_count, 1u);
    EXPECT_EQ(skin_table.stats.matrix_count, 1u);
    ASSERT_EQ(skin_table.ranges.size(), 1u);
    EXPECT_EQ(skin_table.ranges[0].instance, handle);
    EXPECT_EQ(skin_table.ranges[0].source_skin_index, 0u);
    EXPECT_EQ(skin_table.ranges[0].matrix_begin, 0u);
    EXPECT_EQ(skin_table.ranges[0].matrix_count, 1u);
    ASSERT_EQ(skin_table.matrices.size(), 1u);
    EXPECT_NEAR(skin_table.matrices[0][3].x, 4.0f, 1.0e-5f);

    surfaces.clear();
    EXPECT_TRUE(surfaces.draws.empty());
    EXPECT_TRUE(surfaces.render_model_skins.ranges.empty());
    EXPECT_TRUE(surfaces.render_model_skins.matrices.empty());
}

TEST(RenderPreparedModelDraws, DropsStaleHiddenAndMissingInputs)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_primitive_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    const auto hidden = scene.static_model_instance_handles[0];
    auto* hidden_instance = scene.model_instances.get(hidden);
    ASSERT_NE(hidden_instance, nullptr);
    hidden_instance->visible = false;

    nw::render::ModelInstance stale_instance;
    stale_instance.kind = nw::render::ModelInstanceKind::render_model;
    const auto stale = scene.model_instances.create(std::move(stale_instance));
    scene.model_instances.destroy(stale);

    nw::render::ModelInstance missing_instance;
    missing_instance.kind = nw::render::ModelInstanceKind::render_model;
    missing_instance.render_model_index = 99u;
    const auto missing = scene.model_instances.create(std::move(missing_instance));

    const std::array handles{stale, hidden, missing};
    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, std::span<const nw::render::ModelInstanceHandle>{handles});

    EXPECT_TRUE(prepared.common.draws.empty());
    ASSERT_EQ(prepared.common.instance_offsets.size(), 4u);
    EXPECT_EQ(prepared.common.instance_offsets[0], 0u);
    EXPECT_EQ(prepared.common.instance_offsets[1], 0u);
    EXPECT_EQ(prepared.common.instance_offsets[2], 0u);
    EXPECT_EQ(prepared.common.instance_offsets[3], 0u);
    EXPECT_EQ(prepared.common.stats.handle_count, 3u);
    EXPECT_EQ(prepared.common.stats.stale_handle_count, 1u);
    EXPECT_EQ(prepared.common.stats.hidden_instance_count, 1u);
    EXPECT_EQ(prepared.common.stats.missing_asset_count, 1u);
    EXPECT_TRUE(prepared.ranges.ranges.empty());
    EXPECT_EQ(prepared.ranges.stats.handle_count, 3u);
    EXPECT_EQ(prepared.ranges.stats.empty_range_count, 3u);

    const auto validation = viewer::validate_prepared_model_draws(
        scene,
        prepared,
        std::span<const nw::render::ModelInstanceHandle>{handles});
    EXPECT_TRUE(validation.valid());
    EXPECT_EQ(validation.expected_stats.stale_handle_count, 1u);
    EXPECT_EQ(validation.expected_stats.hidden_instance_count, 1u);
    EXPECT_EQ(validation.expected_stats.missing_asset_count, 1u);
}

TEST(RenderPreparedModelDraws, AcceptsNwnLegacySidecarInstances)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    auto model = std::make_unique<nwn::ModelInstance>();
    model->render_enabled = true;
    scene.add(std::move(model));
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.model_instance_handles));

    EXPECT_TRUE(prepared.common.draws.empty());
    EXPECT_TRUE(prepared.nwn_draws.empty());
    ASSERT_EQ(prepared.common.instance_offsets.size(), 2u);
    EXPECT_EQ(prepared.common.instance_offsets[0], 0u);
    EXPECT_EQ(prepared.common.instance_offsets[1], 0u);
    EXPECT_EQ(prepared.common.stats.handle_count, 1u);
    EXPECT_EQ(prepared.common.stats.visible_instance_count, 1u);
    EXPECT_EQ(prepared.common.stats.nwn_legacy_instance_count, 1u);

    const auto validation = viewer::validate_prepared_model_draws(
        scene,
        prepared,
        handle_span(scene.model_instance_handles));
    EXPECT_TRUE(validation.valid());
    EXPECT_EQ(validation.expected_stats.nwn_legacy_instance_count, 1u);
    EXPECT_EQ(validation.expected_stats.nwn_legacy_draw_count, 0u);
}

TEST(RenderPreparedModelDraws, SelectsNwnLegacySidecarDrawItems)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    nwn::Mesh mesh_a;
    mesh_a.uses_plt = true;
    nwn::Mesh mesh_b;
    mesh_b.material_uses_fallback = true;
    viewer::PreviewPreparedModelDraws prepared;
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{.mesh = &mesh_a});
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{.mesh = &mesh_b});
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{});

    const auto make_nwn_draw = [](uint32_t source_draw_index, bool material_uses_fallback = false) {
        nw::render::PreparedModelDraw draw;
        draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
        draw.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
        draw.source_draw_index = source_draw_index;
        draw.material_uses_fallback = material_uses_fallback;
        draw.material_payload = nw::render::prepared_nwn_legacy_material_payload_kind(
            source_draw_index == 0u,
            material_uses_fallback);
        return draw;
    };

    std::array<nw::render::PreparedModelDraw, 5> draws{
        make_nwn_draw(1u, true),
        make_nwn_draw(99u),
        nw::render::PreparedModelDraw{},
        make_nwn_draw(2u),
        make_nwn_draw(0u),
    };

    std::vector<const nwn::PreparedDrawItem*> selected;
    selected.push_back(nullptr);
    const auto stats = viewer::collect_nwn_legacy_prepared_draw_items(
        selected,
        prepared,
        std::span<const nw::render::PreparedModelDraw>{draws});

    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0], &prepared.nwn_draws[1]);
    EXPECT_EQ(selected[1], &prepared.nwn_draws[0]);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.input_draw_count, 5u);
    EXPECT_EQ(stats.selected_draw_count, 2u);
    EXPECT_EQ(stats.non_nwn_draw_count, 1u);
    EXPECT_EQ(stats.missing_sidecar_draw_count, 1u);
    EXPECT_EQ(stats.invalid_sidecar_draw_count, 1u);
    EXPECT_EQ(stats.dropped_draw_count(), 3u);
}

TEST(RenderPreparedModelDraws, SelectsNwnLegacySidecarDrawItemsFromSurfaces)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    nwn::Mesh mesh_a;
    mesh_a.uses_plt = true;
    nwn::Mesh mesh_b;
    mesh_b.material_uses_fallback = true;
    viewer::PreviewPreparedModelDraws prepared;
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{.mesh = &mesh_a});
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{.mesh = &mesh_b});
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{});

    const auto make_nwn_draw = [](uint32_t source_draw_index, bool material_uses_fallback = false) {
        nw::render::PreparedModelDraw draw;
        draw.instance = nw::render::ModelInstanceHandle{.index = 4u, .generation = 1u};
        draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
        draw.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
        draw.instance_source_index = 7u;
        draw.source_draw_index = source_draw_index;
        draw.material_uses_fallback = material_uses_fallback;
        draw.material_payload = nw::render::prepared_nwn_legacy_material_payload_kind(
            source_draw_index == 0u,
            material_uses_fallback);
        return draw;
    };
    prepared.common.draws = {
        make_nwn_draw(1u, true),
        make_nwn_draw(99u),
        nw::render::PreparedModelDraw{},
        make_nwn_draw(2u),
        make_nwn_draw(0u),
    };

    const auto make_surface = [&](uint32_t draw_index) {
        const auto& draw = prepared.common.draws[draw_index];
        nw::render::PreparedModelSurfaceDraw surface;
        surface.instance = draw.instance;
        surface.instance_kind = draw.instance_kind;
        surface.payload_kind = draw.kind;
        surface.draw_index = draw_index;
        surface.instance_source_index = draw.instance_source_index;
        surface.source_draw_index = draw.source_draw_index;
        surface.material_index = draw.material_index;
        surface.skin_index = draw.skin_index;
        surface.material_mode = draw.material_mode;
        surface.material_uses_fallback = draw.material_uses_fallback;
        surface.material_payload = draw.material_payload;
        surface.skinned = draw.skinned;
        surface.world = draw.world;
        surface.normal_matrix = draw.normal_matrix;
        return surface;
    };

    std::array<nw::render::PreparedModelSurfaceDraw, 5> surfaces{
        make_surface(0u),
        make_surface(1u),
        make_surface(2u),
        make_surface(3u),
        make_surface(4u),
    };

    viewer::PreviewPreparedNwnLegacySurfacePacketList packets;
    viewer::collect_nwn_legacy_prepared_surface_packets(
        packets,
        prepared,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces});

    ASSERT_EQ(packets.surface_indices.size(), 2u);
    ASSERT_LT(packets.surface_indices[0], surfaces.size());
    ASSERT_LT(packets.surface_indices[1], surfaces.size());
    EXPECT_EQ(surfaces[packets.surface_indices[0]].source_draw_index, 1u);
    EXPECT_EQ(surfaces[packets.surface_indices[1]].source_draw_index, 0u);
    EXPECT_FALSE(packets.stats.valid());
    EXPECT_EQ(packets.stats.input_draw_count, 5u);
    EXPECT_EQ(packets.stats.selected_draw_count, 2u);
    EXPECT_EQ(packets.stats.non_nwn_draw_count, 1u);
    EXPECT_EQ(packets.stats.missing_sidecar_draw_count, 1u);
    EXPECT_EQ(packets.stats.invalid_sidecar_draw_count, 1u);
    EXPECT_EQ(packets.stats.dropped_draw_count(), 3u);

    std::vector<const nwn::PreparedDrawItem*> selected;
    selected.push_back(nullptr);
    const auto stats = viewer::collect_nwn_legacy_prepared_surface_draw_items(
        selected,
        prepared,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces});

    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0], &prepared.nwn_draws[1]);
    EXPECT_EQ(selected[1], &prepared.nwn_draws[0]);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.input_draw_count, 5u);
    EXPECT_EQ(stats.selected_draw_count, 2u);
    EXPECT_EQ(stats.non_nwn_draw_count, 1u);
    EXPECT_EQ(stats.missing_sidecar_draw_count, 1u);
    EXPECT_EQ(stats.invalid_sidecar_draw_count, 1u);
    EXPECT_EQ(stats.dropped_draw_count(), 3u);
}

TEST(RenderPreparedModelDraws, SurfaceSubmissionAdapterRoutesNwnLegacySelection)
{
    namespace nwn = nw::render::nwn;
    namespace viewer = nw::render::viewer;

    nwn::Mesh opaque_mesh;
    opaque_mesh.material_mode = nw::render::MaterialMode::opaque;
    nwn::Mesh water_mesh;
    water_mesh.material_mode = nw::render::MaterialMode::water;
    viewer::PreviewScene scene;
    viewer::PreviewPreparedModelDraws prepared;
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{.mesh = &opaque_mesh});
    prepared.nwn_draws.push_back(nwn::PreparedDrawItem{.mesh = &water_mesh});

    const auto make_draw = [](const nwn::Mesh& mesh, uint32_t source_draw_index) {
        nw::render::PreparedModelDraw draw;
        draw.instance = nw::render::ModelInstanceHandle{.index = 4u + source_draw_index, .generation = 1u};
        draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
        draw.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
        draw.instance_source_index = 7u + source_draw_index;
        draw.source_draw_index = source_draw_index;
        draw.material_mode = mesh.material_mode;
        draw.material_uses_fallback = mesh.material_uses_fallback;
        draw.material_payload = nw::render::prepared_nwn_legacy_material_payload_kind(
            mesh.uses_plt,
            mesh.material_uses_fallback);
        return draw;
    };
    prepared.common.draws.push_back(make_draw(opaque_mesh, 0u));
    prepared.common.draws.push_back(make_draw(water_mesh, 1u));

    const auto make_surface = [](const nw::render::PreparedModelDraw& draw, uint32_t draw_index) {
        nw::render::PreparedModelSurfaceDraw surface;
        surface.instance = draw.instance;
        surface.instance_kind = draw.instance_kind;
        surface.payload_kind = draw.kind;
        surface.draw_index = draw_index;
        surface.instance_source_index = draw.instance_source_index;
        surface.source_draw_index = draw.source_draw_index;
        surface.material_index = draw.material_index;
        surface.skin_index = draw.skin_index;
        surface.material_mode = draw.material_mode;
        surface.material_uses_fallback = draw.material_uses_fallback;
        surface.material_payload = draw.material_payload;
        surface.skinned = draw.skinned;
        surface.world = draw.world;
        surface.normal_matrix = draw.normal_matrix;
        return surface;
    };
    const std::array surfaces{
        make_surface(prepared.common.draws[0], 0u),
        make_surface(prepared.common.draws[1], 1u),
    };

    std::vector<const nwn::PreparedDrawItem*> selected;
    const auto stats = viewer::collect_nwn_legacy_prepared_surface_draw_items(
        selected,
        prepared,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces},
        nw::render::RenderPassSelection::opaque_cutout);

    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], &prepared.nwn_draws[0]);
    EXPECT_EQ(stats.selected_draw_count, 1u);
    EXPECT_EQ(stats.input_draw_count, 1u);
    EXPECT_EQ(stats.dropped_draw_count(), 0u);

    const auto water_stats = viewer::collect_nwn_legacy_prepared_surface_draw_items(
        selected,
        prepared,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces},
        nw::render::RenderPassSelection::water);

    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], &prepared.nwn_draws[1]);
    EXPECT_EQ(water_stats.selected_draw_count, 1u);
    EXPECT_EQ(water_stats.input_draw_count, 1u);
    EXPECT_EQ(water_stats.dropped_draw_count(), 0u);
}

TEST(RenderPreparedModelDraws, RenderModelSurfaceSubmissionFlagsMissingSources)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.static_models.push_back(nullptr);

    nw::render::PreparedModelSurfaceDraw null_model_surface;
    null_model_surface.instance = nw::render::ModelInstanceHandle{.index = 0u, .generation = 1u};
    null_model_surface.instance_kind = nw::render::ModelInstanceKind::render_model;
    null_model_surface.payload_kind = nw::render::PreparedModelDrawKind::render_model;
    null_model_surface.instance_source_index = 0u;
    null_model_surface.source_draw_index = 0u;
    null_model_surface.material_mode = nw::render::MaterialMode::opaque;

    nw::render::PreparedModelSurfaceDraw out_of_range_surface = null_model_surface;
    out_of_range_surface.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    out_of_range_surface.instance_source_index = 7u;

    const std::array surfaces{
        null_model_surface,
        out_of_range_surface,
    };

    nw::render::ModelRenderContext render_model_ctx;
    const auto stats = viewer::render_prepared_render_model_surface_draws(
        render_model_ctx,
        nullptr,
        scene,
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces},
        nw::render::RenderContext{},
        nw::render::RenderPassSelection::opaque_cutout);

    EXPECT_EQ(stats.submitted_surface_count, 0u);
    EXPECT_EQ(stats.submitted_run_count, 0u);
    EXPECT_EQ(stats.dropped_invalid_surface_count, 2u);
    EXPECT_FALSE(stats.valid());
}

TEST(RenderPreparedModelDraws, ValidationDetectsPreparedDrawMismatch)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_primitive_render_model());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    viewer::PreviewPreparedModelDraws prepared;
    viewer::collect_prepared_model_draws(prepared, scene, handle_span(scene.static_model_instance_handles));
    ASSERT_EQ(prepared.common.draws.size(), 2u);

    prepared.common.draws.pop_back();
    const auto validation = viewer::validate_prepared_model_draws(
        scene,
        prepared,
        handle_span(scene.static_model_instance_handles));

    EXPECT_FALSE(validation.valid());
    EXPECT_GT(validation.protocol_mismatch_count, 0u);
    EXPECT_EQ(validation.expected_stats.render_model_draw_count, 2u);
    EXPECT_EQ(validation.prepared_stats.render_model_draw_count, 2u);
    EXPECT_EQ(validation.prepared_draw_count, 1u);
}

TEST(RenderPreparedModelDraws, DrawSpanRejectsInvalidOffsets)
{
    nw::render::PreparedModelDrawList list;
    list.draws.push_back(nw::render::PreparedModelDraw{});

    list.instance_offsets = {0u};
    EXPECT_TRUE(nw::render::prepared_model_draws_for_handle_index(list, 0).empty());

    list.instance_offsets = {0u, 2u};
    EXPECT_TRUE(nw::render::prepared_model_draws_for_handle_index(list, 0).empty());

    list.instance_offsets = {1u, 0u};
    EXPECT_TRUE(nw::render::prepared_model_draws_for_handle_index(list, 0).empty());

    list.instance_offsets = {0u, 1u};
    EXPECT_EQ(nw::render::prepared_model_draws_for_handle_index(list, 0).size(), 1u);
}

TEST(RenderPreparedModelDraws, CollectsPreparedDrawRanges)
{
    nw::render::PreparedModelDrawList list;
    const nw::render::ModelInstanceHandle render_handle{.index = 7u, .generation = 1u};
    const nw::render::ModelInstanceHandle nwn_handle{.index = 8u, .generation = 1u};

    nw::render::PreparedModelDraw render_draw_a;
    render_draw_a.instance = render_handle;
    render_draw_a.instance_kind = nw::render::ModelInstanceKind::render_model;
    render_draw_a.kind = nw::render::PreparedModelDrawKind::render_model;
    render_draw_a.instance_source_index = 3u;
    render_draw_a.source_draw_index = 0u;
    nw::render::PreparedModelDraw render_draw_b = render_draw_a;
    render_draw_b.source_draw_index = 1u;

    nw::render::PreparedModelDraw nwn_draw;
    nwn_draw.instance = nwn_handle;
    nwn_draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
    nwn_draw.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
    nwn_draw.instance_source_index = 4u;
    nwn_draw.source_draw_index = 2u;

    list.draws = {render_draw_a, render_draw_b, nwn_draw};
    list.instance_offsets = {0u, 2u, 2u, 3u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);

    EXPECT_TRUE(ranges.stats.valid());
    EXPECT_EQ(ranges.stats.handle_count, 3u);
    EXPECT_EQ(ranges.stats.empty_range_count, 1u);
    EXPECT_EQ(ranges.stats.range_count, 2u);
    EXPECT_EQ(ranges.stats.draw_count, 3u);
    EXPECT_EQ(ranges.stats.render_model_range_count, 1u);
    EXPECT_EQ(ranges.stats.nwn_legacy_range_count, 1u);

    ASSERT_EQ(ranges.ranges.size(), 2u);
    EXPECT_EQ(ranges.ranges[0].handle_index, 0u);
    EXPECT_EQ(ranges.ranges[0].draw_begin, 0u);
    EXPECT_EQ(ranges.ranges[0].draw_count, 2u);
    EXPECT_EQ(ranges.ranges[0].instance, render_handle);
    EXPECT_EQ(ranges.ranges[0].kind, nw::render::PreparedModelDrawKind::render_model);
    EXPECT_EQ(ranges.ranges[0].instance_source_index, 3u);
    EXPECT_EQ(nw::render::prepared_model_draws_for_range(list, ranges.ranges[0]).size(), 2u);

    EXPECT_EQ(ranges.ranges[1].handle_index, 2u);
    EXPECT_EQ(ranges.ranges[1].draw_begin, 2u);
    EXPECT_EQ(ranges.ranges[1].draw_count, 1u);
    EXPECT_EQ(ranges.ranges[1].instance, nwn_handle);
    EXPECT_EQ(ranges.ranges[1].kind, nw::render::PreparedModelDrawKind::nwn_legacy);
    EXPECT_EQ(ranges.ranges[1].instance_source_index, 4u);
    EXPECT_EQ(nw::render::prepared_model_draws_for_range(list, ranges.ranges[1]).size(), 1u);
}

TEST(RenderPreparedModelDraws, CollectsPreparedSurfaceDrawsFromRanges)
{
    nw::render::PreparedModelDrawList list;
    const nw::render::ModelInstanceHandle render_handle{.index = 7u, .generation = 1u};
    const nw::render::ModelInstanceHandle nwn_handle{.index = 8u, .generation = 1u};

    nw::render::PreparedModelDraw render_opaque;
    render_opaque.instance = render_handle;
    render_opaque.instance_kind = nw::render::ModelInstanceKind::render_model;
    render_opaque.kind = nw::render::PreparedModelDrawKind::render_model;
    render_opaque.instance_source_index = 3u;
    render_opaque.source_draw_index = 0u;
    render_opaque.material_index = 1u;
    render_opaque.skin_index = 2u;
    render_opaque.material_mode = nw::render::MaterialMode::opaque;
    render_opaque.skinned = true;
    render_opaque.world = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f});
    render_opaque.bounds = nw::render::Bounds{
        .min = {1.0f, 2.0f, 3.0f},
        .max = {2.0f, 3.0f, 4.0f},
    };

    nw::render::PreparedModelDraw render_water = render_opaque;
    render_water.source_draw_index = 1u;
    render_water.material_index = 4u;
    render_water.material_mode = nw::render::MaterialMode::water;
    render_water.skinned = false;

    nw::render::PreparedModelDraw nwn_cutout;
    nwn_cutout.instance = nwn_handle;
    nwn_cutout.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
    nwn_cutout.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
    nwn_cutout.instance_source_index = 4u;
    nwn_cutout.source_draw_index = 2u;
    nwn_cutout.material_mode = nw::render::MaterialMode::cutout;
    nwn_cutout.material_uses_fallback = true;
    nwn_cutout.material_payload = nw::render::PreparedModelMaterialPayloadKind::fallback;

    nw::render::PreparedModelDraw nwn_transparent = nwn_cutout;
    nwn_transparent.source_draw_index = 3u;
    nwn_transparent.material_mode = nw::render::MaterialMode::transparent;
    nwn_transparent.material_uses_fallback = false;
    nwn_transparent.material_payload = nw::render::PreparedModelMaterialPayloadKind::nwn_legacy_material;

    list.draws = {render_opaque, render_water, nwn_cutout, nwn_transparent};
    list.instance_offsets = {0u, 2u, 4u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);

    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);

    EXPECT_TRUE(surfaces.stats.valid());
    EXPECT_EQ(surfaces.stats.range_count, 2u);
    EXPECT_EQ(surfaces.stats.draw_count, 4u);
    EXPECT_EQ(surfaces.stats.render_model_draw_count, 2u);
    EXPECT_EQ(surfaces.stats.nwn_legacy_draw_count, 2u);
    EXPECT_EQ(surfaces.stats.shadow_caster_draw_count, 2u);
    ASSERT_EQ(surfaces.draws.size(), 4u);

    EXPECT_EQ(surfaces.draws[0].payload_kind, nw::render::PreparedModelDrawKind::render_model);
    EXPECT_EQ(surfaces.draws[0].instance_kind, nw::render::ModelInstanceKind::render_model);
    EXPECT_EQ(surfaces.draws[0].range_index, 0u);
    EXPECT_EQ(surfaces.draws[0].draw_index, 0u);
    EXPECT_EQ(surfaces.draws[0].handle_index, 0u);
    EXPECT_EQ(surfaces.draws[0].instance_source_index, 3u);
    EXPECT_EQ(surfaces.draws[0].source_draw_index, 0u);
    EXPECT_EQ(surfaces.draws[0].material_index, 1u);
    EXPECT_EQ(surfaces.draws[0].skin_index, 2u);
    EXPECT_TRUE(surfaces.draws[0].skinned);
    EXPECT_TRUE(surfaces.draws[0].casts_shadow);
    EXPECT_NEAR(surfaces.draws[0].world[3].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(surfaces.draws[0].bounds.max.z, 4.0f, 1.0e-5f);

    EXPECT_FALSE(surfaces.draws[1].casts_shadow);
    EXPECT_EQ(surfaces.draws[2].payload_kind, nw::render::PreparedModelDrawKind::nwn_legacy);
    EXPECT_EQ(surfaces.draws[2].instance_kind, nw::render::ModelInstanceKind::nwn_legacy);
    EXPECT_EQ(surfaces.draws[2].range_index, 1u);
    EXPECT_EQ(surfaces.draws[2].draw_index, 2u);
    EXPECT_EQ(surfaces.draws[2].handle_index, 1u);
    EXPECT_EQ(surfaces.draws[2].source_draw_index, 2u);
    EXPECT_TRUE(surfaces.draws[2].material_uses_fallback);
    EXPECT_EQ(surfaces.draws[2].material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);
    EXPECT_TRUE(surfaces.draws[2].casts_shadow);
    EXPECT_EQ(
        surfaces.draws[3].material_payload,
        nw::render::PreparedModelMaterialPayloadKind::nwn_legacy_material);
    EXPECT_FALSE(surfaces.draws[3].casts_shadow);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceMaterialStatsCountPasses)
{
    const auto make_surface = [](nw::render::MaterialMode mode) {
        nw::render::PreparedModelSurfaceDraw draw;
        draw.material_mode = mode;
        return draw;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> draws{
        make_surface(nw::render::MaterialMode::opaque),
        make_surface(nw::render::MaterialMode::cutout),
        make_surface(nw::render::MaterialMode::water),
        make_surface(nw::render::MaterialMode::transparent),
        make_surface(static_cast<nw::render::MaterialMode>(255)),
    };

    const auto stats = nw::render::prepared_model_surface_material_stats(
        std::span<const nw::render::PreparedModelSurfaceDraw>{draws.data(), draws.size()});

    EXPECT_EQ(stats.opaque_count, 1u);
    EXPECT_EQ(stats.cutout_count, 1u);
    EXPECT_EQ(stats.water_count, 1u);
    EXPECT_EQ(stats.transparent_count, 1u);
    EXPECT_EQ(stats.invalid_mode_count, 1u);
    EXPECT_EQ(stats.total(), 5u);
    EXPECT_FALSE(nw::render::prepared_model_surface_casts_shadow(static_cast<nw::render::MaterialMode>(255)));
}

TEST(RenderPreparedModelDraws, PreparedSurfaceShadowRangesMarkCastersAndDropInvalidRanges)
{
    const auto make_surface = [](uint32_t range_index, bool casts_shadow) {
        nw::render::PreparedModelSurfaceDraw draw;
        draw.range_index = range_index;
        draw.casts_shadow = casts_shadow;
        return draw;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> draws{
        make_surface(0u, true),
        make_surface(0u, true),
        make_surface(1u, false),
        make_surface(2u, true),
        make_surface(99u, true),
    };
    std::vector<uint8_t> shadow_ranges;

    const auto stats = nw::render::collect_prepared_model_surface_shadow_ranges(
        shadow_ranges,
        std::span<const nw::render::PreparedModelSurfaceDraw>{draws.data(), draws.size()},
        3u);

    ASSERT_EQ(shadow_ranges.size(), 3u);
    EXPECT_EQ(shadow_ranges[0], 1u);
    EXPECT_EQ(shadow_ranges[1], 0u);
    EXPECT_EQ(shadow_ranges[2], 1u);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.range_count, 3u);
    EXPECT_EQ(stats.surface_count, 5u);
    EXPECT_EQ(stats.shadow_surface_count, 4u);
    EXPECT_EQ(stats.shadow_range_count, 2u);
    EXPECT_EQ(stats.invalid_range_index_count, 1u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceMaterialBindingsValidateSourceDrawParity)
{
    nw::render::PreparedModelDrawList list;
    const nw::render::ModelInstanceHandle handle{.index = 7u, .generation = 1u};

    nw::render::PreparedModelDraw cutout_draw;
    cutout_draw.instance = handle;
    cutout_draw.instance_kind = nw::render::ModelInstanceKind::render_model;
    cutout_draw.kind = nw::render::PreparedModelDrawKind::render_model;
    cutout_draw.instance_source_index = 3u;
    cutout_draw.source_draw_index = 8u;
    cutout_draw.material_index = 1u;
    cutout_draw.skin_index = 2u;
    cutout_draw.material_mode = nw::render::MaterialMode::cutout;
    cutout_draw.skinned = true;

    nw::render::PreparedModelDraw transparent_draw = cutout_draw;
    transparent_draw.source_draw_index = 9u;
    transparent_draw.material_index = 4u;
    transparent_draw.skin_index = nw::render::kInvalidPreparedModelDrawIndex;
    transparent_draw.material_mode = nw::render::MaterialMode::transparent;
    transparent_draw.skinned = false;

    list.draws = {cutout_draw, transparent_draw};
    list.instance_offsets = {0u, 2u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);
    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);
    nw::render::sort_prepared_model_surface_draws_by_pass(
        std::span<nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()});

    const auto stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        list,
        ranges);

    EXPECT_TRUE(stats.valid());
    EXPECT_EQ(stats.surface_count, 2u);
    EXPECT_EQ(stats.bound_surface_count, 2u);
    EXPECT_EQ(stats.material_fallback_count, 0u);
    EXPECT_EQ(stats.fallback_material_payload_count, 0u);
    EXPECT_EQ(stats.materials.cutout_count, 1u);
    EXPECT_EQ(stats.materials.transparent_count, 1u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceMaterialBindingsCountFallbackMaterials)
{
    nw::render::PreparedModelDrawList list;

    nw::render::PreparedModelDraw nwn_draw;
    nwn_draw.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    nwn_draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
    nwn_draw.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
    nwn_draw.instance_source_index = 2u;
    nwn_draw.source_draw_index = 3u;
    nwn_draw.material_mode = nw::render::MaterialMode::opaque;
    nwn_draw.material_uses_fallback = true;
    nwn_draw.material_payload = nw::render::PreparedModelMaterialPayloadKind::fallback;

    nw::render::PreparedModelDraw render_model_draw = nwn_draw;
    render_model_draw.instance = nw::render::ModelInstanceHandle{.index = 2u, .generation = 1u};
    render_model_draw.instance_kind = nw::render::ModelInstanceKind::render_model;
    render_model_draw.kind = nw::render::PreparedModelDrawKind::render_model;
    render_model_draw.instance_source_index = 4u;
    render_model_draw.source_draw_index = 5u;

    list.draws = {nwn_draw, render_model_draw};
    list.instance_offsets = {0u, 1u, 2u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);
    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);

    ASSERT_EQ(surfaces.draws.size(), 2u);
    EXPECT_TRUE(surfaces.draws[0].material_uses_fallback);
    EXPECT_TRUE(surfaces.draws[1].material_uses_fallback);
    EXPECT_EQ(surfaces.draws[0].material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);
    EXPECT_EQ(surfaces.draws[1].material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);

    auto stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.bound_surface_count, 2u);
    EXPECT_EQ(stats.material_fallback_count, 2u);
    EXPECT_EQ(stats.render_model_material_fallback_count, 1u);
    EXPECT_EQ(stats.nwn_legacy_material_fallback_count, 1u);
    EXPECT_EQ(stats.fallback_material_payload_count, 2u);
    EXPECT_EQ(stats.render_model_fallback_material_payload_count, 1u);
    EXPECT_EQ(stats.nwn_legacy_fallback_material_payload_count, 1u);
    EXPECT_EQ(stats.material_mismatch_count, 0u);

    for (auto& surface : surfaces.draws) {
        surface.material_uses_fallback = false;
    }
    stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.bound_surface_count, 0u);
    EXPECT_EQ(stats.material_fallback_count, 0u);
    EXPECT_EQ(stats.render_model_material_fallback_count, 0u);
    EXPECT_EQ(stats.nwn_legacy_material_fallback_count, 0u);
    EXPECT_EQ(stats.fallback_material_payload_count, 0u);
    EXPECT_EQ(stats.render_model_fallback_material_payload_count, 0u);
    EXPECT_EQ(stats.nwn_legacy_fallback_material_payload_count, 0u);
    EXPECT_EQ(stats.material_mismatch_count, 2u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceMaterialBindingsCountFallbackPayloads)
{
    nw::render::PreparedModelDrawList list;

    nw::render::PreparedModelDraw draw;
    draw.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
    draw.kind = nw::render::PreparedModelDrawKind::nwn_legacy;
    draw.instance_source_index = 2u;
    draw.source_draw_index = 3u;
    draw.material_mode = nw::render::MaterialMode::opaque;
    draw.material_payload = nw::render::PreparedModelMaterialPayloadKind::fallback;

    list.draws = {draw};
    list.instance_offsets = {0u, 1u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);
    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);

    ASSERT_EQ(surfaces.draws.size(), 1u);
    EXPECT_FALSE(surfaces.draws[0].material_uses_fallback);
    EXPECT_EQ(surfaces.draws[0].material_payload, nw::render::PreparedModelMaterialPayloadKind::fallback);

    const auto stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.bound_surface_count, 1u);
    EXPECT_EQ(stats.material_fallback_count, 0u);
    EXPECT_EQ(stats.fallback_material_payload_count, 1u);
    EXPECT_EQ(stats.render_model_fallback_material_payload_count, 0u);
    EXPECT_EQ(stats.nwn_legacy_fallback_material_payload_count, 1u);
    EXPECT_EQ(stats.material_mismatch_count, 0u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceMaterialBindingsCountInvalidBoundaries)
{
    nw::render::PreparedModelDrawList list;
    const nw::render::ModelInstanceHandle handle{.index = 7u, .generation = 1u};

    nw::render::PreparedModelDraw draw;
    draw.instance = handle;
    draw.instance_kind = nw::render::ModelInstanceKind::render_model;
    draw.kind = nw::render::PreparedModelDrawKind::render_model;
    draw.instance_source_index = 3u;
    draw.source_draw_index = 8u;
    draw.material_index = 1u;
    draw.skin_index = 2u;
    draw.material_mode = nw::render::MaterialMode::cutout;
    draw.skinned = true;

    list.draws = {draw};
    list.instance_offsets = {0u, 1u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);
    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);
    ASSERT_EQ(surfaces.draws.size(), 1u);

    std::array<nw::render::PreparedModelSurfaceDraw, 1> invalid_range{surfaces.draws[0]};
    invalid_range[0].range_index = 99u;
    auto stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{invalid_range.data(), invalid_range.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.invalid_range_count, 1u);
    EXPECT_EQ(stats.bound_surface_count, 0u);

    std::array<nw::render::PreparedModelSurfaceDraw, 1> invalid_draw{surfaces.draws[0]};
    invalid_draw[0].draw_index = 99u;
    stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{invalid_draw.data(), invalid_draw.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.invalid_draw_index_count, 1u);
    EXPECT_EQ(stats.bound_surface_count, 0u);

    std::array<nw::render::PreparedModelSurfaceDraw, 1> mismatched{surfaces.draws[0]};
    mismatched[0].source_draw_index = 77u;
    mismatched[0].material_index = 44u;
    mismatched[0].casts_shadow = false;
    stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{mismatched.data(), mismatched.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.payload_mismatch_count, 1u);
    EXPECT_EQ(stats.material_mismatch_count, 1u);
    EXPECT_EQ(stats.shadow_mismatch_count, 1u);
    EXPECT_EQ(stats.bound_surface_count, 0u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceMaterialBindingsRejectInvalidMaterialModes)
{
    nw::render::PreparedModelDrawList list;
    nw::render::PreparedModelDraw draw;
    draw.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    draw.instance_kind = nw::render::ModelInstanceKind::render_model;
    draw.kind = nw::render::PreparedModelDrawKind::render_model;
    draw.instance_source_index = 0u;
    draw.source_draw_index = 0u;
    draw.material_mode = static_cast<nw::render::MaterialMode>(255);
    list.draws = {draw};
    list.instance_offsets = {0u, 1u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);
    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);

    ASSERT_EQ(surfaces.draws.size(), 1u);
    EXPECT_FALSE(surfaces.draws[0].casts_shadow);

    const auto stats = nw::render::validate_prepared_model_surface_material_bindings(
        std::span<const nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        list,
        ranges);
    EXPECT_FALSE(stats.valid());
    EXPECT_EQ(stats.materials.invalid_mode_count, 1u);
    EXPECT_EQ(stats.bound_surface_count, 0u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceDrawsSortInPlaceByPassAndPayload)
{
    EXPECT_EQ(
        nw::render::prepared_model_surface_pass_index(nw::render::MaterialMode::opaque),
        0u);
    EXPECT_EQ(
        nw::render::prepared_model_surface_pass_index(nw::render::MaterialMode::cutout),
        1u);
    EXPECT_EQ(
        nw::render::prepared_model_surface_pass_index(nw::render::MaterialMode::water),
        2u);
    EXPECT_EQ(
        nw::render::prepared_model_surface_pass_index(nw::render::MaterialMode::transparent),
        3u);

    const auto make_surface = [](
                                  nw::render::MaterialMode mode,
                                  nw::render::PreparedModelDrawKind payload_kind,
                                  uint32_t instance_source_index,
                                  uint32_t material_index,
                                  uint32_t source_draw_index) {
        nw::render::PreparedModelSurfaceDraw draw;
        draw.material_mode = mode;
        draw.payload_kind = payload_kind;
        draw.instance_source_index = instance_source_index;
        draw.material_index = material_index;
        draw.source_draw_index = source_draw_index;
        draw.range_index = source_draw_index;
        draw.handle_index = source_draw_index;
        return draw;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> draws{
        make_surface(
            nw::render::MaterialMode::transparent,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            9u,
            2u,
            0u),
        make_surface(
            nw::render::MaterialMode::water,
            nw::render::PreparedModelDrawKind::render_model,
            4u,
            3u,
            1u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            2u,
            1u,
            2u),
        make_surface(
            nw::render::MaterialMode::cutout,
            nw::render::PreparedModelDrawKind::render_model,
            1u,
            0u,
            3u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::render_model,
            3u,
            0u,
            4u),
    };

    nw::render::sort_prepared_model_surface_draws_by_pass(
        std::span<nw::render::PreparedModelSurfaceDraw>{draws.data(), draws.size()});

    ASSERT_EQ(draws.size(), 5u);
    EXPECT_EQ(draws[0].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(draws[0].payload_kind, nw::render::PreparedModelDrawKind::render_model);
    EXPECT_EQ(draws[0].source_draw_index, 4u);
    EXPECT_EQ(draws[1].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(draws[1].payload_kind, nw::render::PreparedModelDrawKind::nwn_legacy);
    EXPECT_EQ(draws[1].source_draw_index, 2u);
    EXPECT_EQ(draws[2].material_mode, nw::render::MaterialMode::cutout);
    EXPECT_EQ(draws[3].material_mode, nw::render::MaterialMode::water);
    EXPECT_EQ(draws[4].material_mode, nw::render::MaterialMode::transparent);
}

TEST(RenderPreparedModelDraws, PreparedSurfacePassViewsUseSortedFlatArray)
{
    const auto make_surface = [](
                                  nw::render::MaterialMode mode,
                                  nw::render::PreparedModelDrawKind payload_kind,
                                  uint32_t source_draw_index) {
        nw::render::PreparedModelSurfaceDraw draw;
        draw.material_mode = mode;
        draw.payload_kind = payload_kind;
        draw.source_draw_index = source_draw_index;
        draw.range_index = source_draw_index;
        draw.handle_index = source_draw_index;
        return draw;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> draws{
        make_surface(
            nw::render::MaterialMode::transparent,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            0u),
        make_surface(
            nw::render::MaterialMode::water,
            nw::render::PreparedModelDrawKind::render_model,
            1u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            2u),
        make_surface(
            nw::render::MaterialMode::cutout,
            nw::render::PreparedModelDrawKind::render_model,
            3u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::render_model,
            4u),
    };

    nw::render::sort_prepared_model_surface_draws_by_pass(
        std::span<nw::render::PreparedModelSurfaceDraw>{draws.data(), draws.size()});
    const std::span<const nw::render::PreparedModelSurfaceDraw> sorted{draws.data(), draws.size()};

    const auto opaque_cutout = nw::render::prepared_model_surface_draws_for_pass_indices(sorted, 0u, 2u);
    ASSERT_EQ(opaque_cutout.size(), 3u);
    EXPECT_EQ(opaque_cutout.data(), draws.data());
    EXPECT_EQ(opaque_cutout[0].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(opaque_cutout[1].material_mode, nw::render::MaterialMode::opaque);
    EXPECT_EQ(opaque_cutout[2].material_mode, nw::render::MaterialMode::cutout);

    const auto water = nw::render::prepared_model_surface_draws_for_pass_indices(sorted, 2u, 3u);
    ASSERT_EQ(water.size(), 1u);
    EXPECT_EQ(water.data(), draws.data() + 3u);
    EXPECT_EQ(water.front().material_mode, nw::render::MaterialMode::water);

    const auto transparent = nw::render::prepared_model_surface_draws_for_pass_indices(sorted, 3u, 4u);
    ASSERT_EQ(transparent.size(), 1u);
    EXPECT_EQ(transparent.data(), draws.data() + 4u);
    EXPECT_EQ(transparent.front().material_mode, nw::render::MaterialMode::transparent);

    EXPECT_TRUE(nw::render::prepared_model_surface_draws_for_pass_indices(sorted, 2u, 2u).empty());
    EXPECT_TRUE(nw::render::prepared_model_surface_draws_for_pass_indices(sorted, 4u, 5u).empty());
}

TEST(RenderPreparedModelDraws, PreparedSurfaceDrawsPartitionRenderModelRunsBySource)
{
    const auto make_surface = [](
                                  nw::render::MaterialMode mode,
                                  nw::render::PreparedModelDrawKind payload_kind,
                                  nw::render::ModelInstanceKind instance_kind,
                                  uint32_t instance_source_index,
                                  uint32_t source_draw_index) {
        nw::render::PreparedModelSurfaceDraw draw;
        draw.material_mode = mode;
        draw.payload_kind = payload_kind;
        draw.instance_kind = instance_kind;
        draw.instance_source_index = instance_source_index;
        draw.source_draw_index = source_draw_index;
        draw.range_index = source_draw_index;
        draw.handle_index = source_draw_index;
        return draw;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> draws{
        make_surface(
            nw::render::MaterialMode::transparent,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            nw::render::ModelInstanceKind::nwn_legacy,
            1u,
            0u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            3u,
            1u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            2u,
            2u),
        make_surface(
            nw::render::MaterialMode::cutout,
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            2u,
            3u),
        make_surface(
            nw::render::MaterialMode::transparent,
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            3u,
            4u),
        make_surface(
            nw::render::MaterialMode::opaque,
            nw::render::PreparedModelDrawKind::nwn_legacy,
            nw::render::ModelInstanceKind::nwn_legacy,
            8u,
            5u),
    };

    nw::render::sort_prepared_model_surface_draws_by_pass(
        std::span<nw::render::PreparedModelSurfaceDraw>{draws.data(), draws.size()});
    const std::span<const nw::render::PreparedModelSurfaceDraw> surfaces{draws.data(), draws.size()};

    auto run = nw::render::next_prepared_render_model_surface_run(surfaces, 0u);
    ASSERT_FALSE(run.empty());
    EXPECT_EQ(run.instance_source_index, 2u);
    ASSERT_EQ(run.draws.size(), 1u);
    EXPECT_EQ(run.draws[0].source_draw_index, 2u);

    run = nw::render::next_prepared_render_model_surface_run(surfaces, run.next);
    ASSERT_FALSE(run.empty());
    EXPECT_EQ(run.instance_source_index, 3u);
    ASSERT_EQ(run.draws.size(), 1u);
    EXPECT_EQ(run.draws[0].source_draw_index, 1u);

    run = nw::render::next_prepared_render_model_surface_run(surfaces, run.next);
    ASSERT_FALSE(run.empty());
    EXPECT_EQ(run.instance_source_index, 2u);
    ASSERT_EQ(run.draws.size(), 1u);
    EXPECT_EQ(run.draws[0].source_draw_index, 3u);

    run = nw::render::next_prepared_render_model_surface_run(surfaces, run.next);
    ASSERT_FALSE(run.empty());
    EXPECT_EQ(run.instance_source_index, 3u);
    ASSERT_EQ(run.draws.size(), 1u);
    EXPECT_EQ(run.draws[0].source_draw_index, 4u);

    run = nw::render::next_prepared_render_model_surface_run(surfaces, run.next);
    EXPECT_TRUE(run.empty());
    EXPECT_EQ(run.next, surfaces.size());
}

TEST(RenderPreparedModelDraws, PreparedRenderModelSurfaceRunsDropInvalidSourceIndices)
{
    const auto make_surface = [](nw::render::PreparedModelDrawKind payload_kind,
                                  nw::render::ModelInstanceKind instance_kind,
                                  uint32_t instance_source_index,
                                  uint32_t source_draw_index) {
        nw::render::PreparedModelSurfaceDraw draw;
        draw.payload_kind = payload_kind;
        draw.instance_kind = instance_kind;
        draw.instance_source_index = instance_source_index;
        draw.source_draw_index = source_draw_index;
        return draw;
    };

    std::vector<nw::render::PreparedModelSurfaceDraw> draws{
        make_surface(
            nw::render::PreparedModelDrawKind::nwn_legacy,
            nw::render::ModelInstanceKind::nwn_legacy,
            0u,
            0u),
        make_surface(
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            1u,
            1u),
        make_surface(
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            1u,
            2u),
        make_surface(
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            4u,
            3u),
        make_surface(
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            nw::render::kInvalidPreparedModelDrawIndex,
            4u),
        make_surface(
            nw::render::PreparedModelDrawKind::render_model,
            nw::render::ModelInstanceKind::render_model,
            0u,
            5u),
    };

    nw::render::PreparedRenderModelSurfaceRunList runs;
    nw::render::collect_prepared_render_model_surface_runs(
        runs,
        std::span<const nw::render::PreparedModelSurfaceDraw>{draws.data(), draws.size()},
        2u);

    EXPECT_FALSE(runs.stats.valid());
    EXPECT_EQ(runs.stats.input_surface_count, 6u);
    EXPECT_EQ(runs.stats.non_render_model_surface_count, 1u);
    EXPECT_EQ(runs.stats.render_model_surface_count, 5u);
    EXPECT_EQ(runs.stats.invalid_source_index_count, 2u);
    EXPECT_EQ(runs.stats.run_count, 2u);
    ASSERT_EQ(runs.runs.size(), 2u);

    EXPECT_EQ(runs.runs[0].instance_source_index, 1u);
    ASSERT_EQ(runs.runs[0].draws.size(), 2u);
    EXPECT_EQ(runs.runs[0].draws[0].source_draw_index, 1u);
    EXPECT_EQ(runs.runs[0].draws[1].source_draw_index, 2u);

    EXPECT_EQ(runs.runs[1].instance_source_index, 0u);
    ASSERT_EQ(runs.runs[1].draws.size(), 1u);
    EXPECT_EQ(runs.runs[1].draws[0].source_draw_index, 5u);
}

TEST(RenderPreparedModelDraws, PreparedSurfaceDrawsRejectInvalidRangesAndMismatches)
{
    nw::render::PreparedModelDrawList list;

    nw::render::PreparedModelDraw draw;
    draw.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    draw.instance_kind = nw::render::ModelInstanceKind::render_model;
    draw.kind = nw::render::PreparedModelDrawKind::render_model;
    draw.instance_source_index = 0u;
    list.draws = {draw};
    list.instance_offsets = {0u, 1u};

    nw::render::PreparedModelDrawRangeList ranges;
    ranges.ranges.push_back(nw::render::PreparedModelDrawRange{
        .handle_index = 0u,
        .draw_begin = 4u,
        .draw_count = 1u,
        .instance = draw.instance,
        .instance_kind = draw.instance_kind,
        .kind = draw.kind,
        .instance_source_index = draw.instance_source_index,
    });
    ranges.ranges.push_back(nw::render::PreparedModelDrawRange{
        .handle_index = 0u,
        .draw_begin = 0u,
        .draw_count = 1u,
        .instance = draw.instance,
        .instance_kind = nw::render::ModelInstanceKind::nwn_legacy,
        .kind = nw::render::PreparedModelDrawKind::nwn_legacy,
        .instance_source_index = draw.instance_source_index,
    });

    nw::render::PreparedModelSurfaceDrawList surfaces;
    nw::render::collect_prepared_model_surface_draws(surfaces, list, ranges);

    EXPECT_FALSE(surfaces.stats.valid());
    EXPECT_EQ(surfaces.stats.range_count, 2u);
    EXPECT_EQ(surfaces.stats.invalid_range_count, 1u);
    EXPECT_EQ(surfaces.stats.range_mismatch_count, 1u);
    EXPECT_EQ(surfaces.stats.draw_count, 0u);
    EXPECT_TRUE(surfaces.draws.empty());
}

TEST(RenderPreparedModelDraws, DrawRangesRejectInvalidOffsetsAndMixedRanges)
{
    nw::render::PreparedModelDrawList list;

    nw::render::PreparedModelDraw draw_a;
    draw_a.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    draw_a.instance_kind = nw::render::ModelInstanceKind::render_model;
    draw_a.kind = nw::render::PreparedModelDrawKind::render_model;
    draw_a.instance_source_index = 0u;
    nw::render::PreparedModelDraw draw_b = draw_a;
    draw_b.instance_source_index = 1u;

    list.draws = {draw_a, draw_b};
    list.instance_offsets = {0u, 2u, 5u, 1u, 1u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);

    EXPECT_FALSE(ranges.stats.valid());
    EXPECT_EQ(ranges.stats.handle_count, 4u);
    EXPECT_EQ(ranges.stats.mixed_range_count, 1u);
    EXPECT_EQ(ranges.stats.invalid_offset_range_count, 2u);
    EXPECT_EQ(ranges.stats.empty_range_count, 1u);
    EXPECT_EQ(ranges.stats.range_count, 0u);
    EXPECT_TRUE(ranges.ranges.empty());
}

TEST(RenderPreparedModelDraws, DrawRangesRejectKindInstanceMismatch)
{
    nw::render::PreparedModelDrawList list;

    nw::render::PreparedModelDraw draw;
    draw.instance = nw::render::ModelInstanceHandle{.index = 1u, .generation = 1u};
    draw.instance_kind = nw::render::ModelInstanceKind::nwn_legacy;
    draw.kind = nw::render::PreparedModelDrawKind::render_model;
    draw.instance_source_index = 0u;
    list.draws = {draw};
    list.instance_offsets = {0u, 1u};

    nw::render::PreparedModelDrawRangeList ranges;
    nw::render::collect_prepared_model_draw_ranges(ranges, list);

    EXPECT_FALSE(ranges.stats.valid());
    EXPECT_EQ(ranges.stats.invalid_kind_range_count, 1u);
    EXPECT_EQ(ranges.stats.range_count, 0u);
    EXPECT_TRUE(ranges.ranges.empty());
}

TEST(RenderPreparedModelDraws, DrawRangeSpanRejectsInvalidRange)
{
    nw::render::PreparedModelDrawList list;
    list.draws.push_back(nw::render::PreparedModelDraw{});

    nw::render::PreparedModelDrawRange out_of_bounds_begin;
    out_of_bounds_begin.draw_begin = 2u;
    out_of_bounds_begin.draw_count = 1u;
    EXPECT_TRUE(nw::render::prepared_model_draws_for_range(list, out_of_bounds_begin).empty());

    nw::render::PreparedModelDrawRange out_of_bounds_count;
    out_of_bounds_count.draw_begin = 0u;
    out_of_bounds_count.draw_count = 2u;
    EXPECT_TRUE(nw::render::prepared_model_draws_for_range(list, out_of_bounds_count).empty());

    nw::render::PreparedModelDrawRange valid_range;
    valid_range.draw_begin = 0u;
    valid_range.draw_count = 1u;
    EXPECT_EQ(nw::render::prepared_model_draws_for_range(list, valid_range).size(), 1u);
}
