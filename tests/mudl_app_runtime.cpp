#include "app_runtime.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <memory>
#include <utility>

namespace {

std::unique_ptr<nw::render::RenderModel> make_sequence_model()
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = "sequence_test";
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    model->nodes.push_back(nw::render::Node{});
    return model;
}

} // namespace

TEST(MudlAppRuntime, DefaultsToToolsetVisualMode)
{
    const mudl::AppState state{};

    EXPECT_EQ(state.preview_scene_load_options.visual_render_mode, nw::ObjectVisualRenderMode::toolset);
}

TEST(MudlAppRuntime, VfxSequenceUsesCommonModelInstances)
{
    mudl::AppState state;
    state.current_scene = std::make_unique<nw::render::viewer::PreviewScene>();
    state.current_scene->add(make_sequence_model());
    ASSERT_EQ(state.current_scene->static_model_instance_handles.size(), 1u);

    mudl::VfxSequence sequence;
    sequence.label = "test";
    mudl::VfxSequenceStep step;
    step.kind = mudl::VfxSequenceStepKind::model;
    step.duration_ms = 1000;
    step.start_offset = {3.0f, 4.0f, 5.0f};
    sequence.steps.push_back(std::move(step));

    ASSERT_TRUE(mudl::vfx_sequence_prepare_scene(state, *state.current_scene, sequence));
    ASSERT_EQ(state.current_scene->static_models.size(), 1u);

    auto* instance = state.current_scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->visible);
    EXPECT_NEAR(instance->root_transform[3].x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(instance->root_transform[3].y, 4.0f, 1.0e-5f);
    EXPECT_NEAR(instance->root_transform[3].z, 5.0f, 1.0e-5f);

    instance->visible = true;
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{9.0f, 9.0f, 9.0f});
    mudl::vfx_sequence_reset_runtime(state);

    instance = state.current_scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->visible);
    EXPECT_NEAR(instance->root_transform[3].x, 3.0f, 1.0e-5f);

    mudl::vfx_sequence_update_runtime(state, 500);

    instance = state.current_scene->static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(instance->visible);
    EXPECT_NEAR(instance->root_transform[3].x, 3.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxSequenceRejectsIncompleteOrderedModelBatch)
{
    mudl::AppState state;
    nw::render::viewer::PreviewScene scene;
    scene.add(make_sequence_model());

    mudl::VfxSequence sequence;
    sequence.steps.resize(2);

    EXPECT_FALSE(mudl::vfx_sequence_prepare_scene(state, scene, sequence));
    EXPECT_TRUE(state.vfx_sequence_steps.empty());
}

TEST(MudlAppRuntime, RenderModelAttachmentSetupResolvesValidRowsAndCountsRejects)
{
    nw::render::viewer::PreviewScene scene;
    auto owner = make_sequence_model();
    owner->sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "hand",
    });
    scene.add(std::move(owner));
    scene.add(make_sequence_model());

    const std::array attachments{
        nw::render::viewer::RenderModelAttachmentSetup{
            .child_model_index = 1u,
            .owner_model_index = 0u,
            .owner_socket = "hand",
            .child_local_transform = glm::translate(
                glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}),
            .child_local_scale = 2.0f,
            .orientation = nw::render::ModelAttachmentOrientationPolicy::owner_space_placement,
            .source_offset = nw::render::ModelAttachmentSourceOffsetPolicy::socket_bind_or_root_translation,
        },
        nw::render::viewer::RenderModelAttachmentSetup{
            .child_model_index = 9u,
            .owner_model_index = 0u,
            .owner_socket = "hand",
        },
    };

    const auto stats = scene.attach_render_models(attachments);

    EXPECT_EQ(stats.input_count, 2u);
    EXPECT_EQ(stats.attached_count, 1u);
    EXPECT_EQ(stats.invalid_model_count, 1u);
    ASSERT_EQ(scene.model_attachments.size(), 1u);
    EXPECT_EQ(scene.model_attachments.front().child_instance_handle,
        scene.static_model_instance_handles[1]);
    EXPECT_EQ(scene.model_attachments.front().owner_instance_handle,
        scene.static_model_instance_handles[0]);
    EXPECT_EQ(scene.model_attachments.front().orientation,
        nw::render::ModelAttachmentOrientationPolicy::owner_space_placement);
    EXPECT_EQ(scene.model_attachments.front().source_offset,
        nw::render::ModelAttachmentSourceOffsetPolicy::socket_bind_or_root_translation);
}

TEST(MudlAppRuntime, VfxSequenceDerivesStepAnimationTimeWhenItBecomesVisible)
{
    mudl::AppState state;
    state.current_scene = std::make_unique<nw::render::viewer::PreviewScene>();
    state.current_scene->add(make_sequence_model());
    state.current_scene->add(make_sequence_model());
    state.vfx_sequence_loop_ms = 1000;
    state.vfx_sequence_steps = {
        {.model_index = 0, .start_ms = 0, .end_ms = 100},
        {.model_index = 1, .start_ms = 100, .end_ms = 1000},
    };

    auto* first = state.current_scene->static_model_instance(0);
    auto* second = state.current_scene->static_model_instance(1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->visible = true;
    second->visible = false;
    second->animation.time = 0.7f;
    state.vfx_sequence_time_ms = 90;

    mudl::vfx_sequence_update_runtime(state, 20);

    EXPECT_FALSE(first->visible);
    EXPECT_TRUE(second->visible);
    EXPECT_NEAR(second->animation.time, 0.01f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxAuthoredAxisUsesCompiledDefaultsWithoutImportFallback)
{
    auto model = make_sequence_model();

    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    ASSERT_EQ(scene.static_models.size(), 1u);

    nw::render::viewer::SceneParticleSystem particles;
    particles.owner_model_index = 0u;
    particles.owner_instance_handle = scene.static_model_instance_handles.front();

    nw::model::ParticleImportEmitterInit init;
    init.emitter = 0u;
    init.emitter_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    init.emitter_node_name = "also_missing_emitter_name";
    init.target_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    init.target_node_name = "missing_target_name";
    particles.import.emitter_inits.push_back(std::move(init));

    nw::render::CompiledParticleEmitter emitter;
    emitter.targeting.mode = nw::render::ParticleTargetingMode::point_gravity;
    emitter.attachment.has_default_position = true;
    emitter.attachment.default_position = glm::vec3{1.0f, 2.0f, 3.0f};
    emitter.attachment.has_default_target_offset = true;
    emitter.attachment.default_target_offset = glm::vec3{1.0f, 2.0f, 7.0f};
    particles.compiled.effect.emitters.push_back(std::move(emitter));
    scene.particles.push_back(std::move(particles));

    const auto axis = mudl::vfx_sequence_authored_axis(scene, 0u);

    ASSERT_TRUE(axis);
    EXPECT_NEAR(axis->x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(axis->y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(axis->z, 1.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxAuthoredAxisUsesCommonAttachmentPoints)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_sequence_model());
    ASSERT_EQ(scene.static_models.size(), 1u);
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    auto* common = scene.static_model_instance(0u);
    ASSERT_NE(common, nullptr);
    common->root_transform = glm::rotate(glm::mat4{1.0f}, 1.57079632679f, glm::vec3{0.0f, 0.0f, 1.0f});
    common->attachment_node_world_transforms.resize(2);
    common->attachment_node_transform_valid.assign(2, 1u);
    common->attachment_node_world_transforms[0] = common->root_transform * glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f});
    common->attachment_node_world_transforms[1] = common->root_transform * glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 2.0f, 3.0f});

    nw::render::viewer::SceneParticleSystem particles;
    particles.owner_model_index = 0u;
    particles.owner_instance_handle = scene.static_model_instance_handles.front();

    nw::render::ParticleEmitterDef import_emitter;
    import_emitter.name = "missing_emitter_name";
    import_emitter.targeting.mode = nw::render::ParticleTargetingMode::point_gravity;
    particles.import.effect.emitters.push_back(std::move(import_emitter));

    nw::model::ParticleImportEmitterInit init;
    init.emitter = 0u;
    init.emitter_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    init.emitter_node_name = "missing_source_node";
    init.target_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    init.target_node_name = "missing_target_node";
    particles.import.emitter_inits.push_back(std::move(init));

    nw::render::CompiledParticleEmitter compiled_emitter;
    compiled_emitter.targeting.mode = nw::render::ParticleTargetingMode::point_gravity;
    compiled_emitter.attachment.emitter_attachment_point = 0u;
    compiled_emitter.attachment.target_attachment_point = 1u;
    particles.compiled.effect.emitters.push_back(std::move(compiled_emitter));

    scene.particles.push_back(std::move(particles));

    const auto axis = mudl::vfx_sequence_authored_axis(scene, 0u);

    ASSERT_TRUE(axis);
    EXPECT_NEAR(axis->x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(axis->y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(axis->z, 0.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxProjectileTransportMatchesParticlesByOwnerHandle)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_sequence_model());
    ASSERT_EQ(scene.static_models.size(), 1u);
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);

    nw::render::viewer::SceneParticleSystem particles;
    particles.owner_model_index = 0u;
    particles.owner_instance_handle = scene.static_model_instance_handles.front();

    nw::render::CompiledParticleEmitter compiled_emitter;
    compiled_emitter.targeting.mode = nw::render::ParticleTargetingMode::point_gravity;
    particles.compiled.effect.emitters.push_back(std::move(compiled_emitter));

    scene.particles.push_back(std::move(particles));

    EXPECT_EQ(
        mudl::vfx_sequence_classify_projectile_transport(scene, 0u),
        mudl::VfxProjectileTransportKind::source_rooted_target_point);
}

TEST(MudlAppRuntime, VfxProjectileRootPositionUsesCachedSourceSocket)
{
    auto model = make_sequence_model();
    model->nodes.front().world_transform = glm::translate(
        glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    model->sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = glm::mat4{1.0f},
        .bind_transform = model->nodes.front().world_transform,
        .name = "muzzle",
    });

    mudl::AppState::VfxSequencePlaybackStep step;
    step.start_ms = 0;
    step.end_ms = 1000;
    step.start_pos = glm::vec3{10.0f, 0.0f, 0.0f};
    step.end_pos = glm::vec3{20.0f, 0.0f, 0.0f};
    step.has_source_anchor = true;
    step.source_anchor_socket_index = model->socket_index("muzzle");
    model->sockets.front().name = "renamed_after_binding";

    const glm::vec3 root_position = mudl::vfx_sequence_projectile_root_position(*model, step, 0.0f, 0u);

    EXPECT_NEAR(root_position.x, 8.0f, 1.0e-5f);
    EXPECT_NEAR(root_position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(root_position.z, 0.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxResolveTargetPointUsesCachedTargetSocket)
{
    auto model = make_sequence_model();
    model->nodes.front().world_transform = glm::translate(
        glm::mat4{1.0f}, glm::vec3{2.0f, 3.0f, 4.0f});
    model->sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = glm::mat4{1.0f},
        .bind_transform = glm::mat4{1.0f},
        .name = "impact",
    });

    const uint32_t socket_index = model->socket_index("impact");
    model->sockets.front().name = "renamed_after_binding";
    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(0u);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(
        glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    ASSERT_TRUE(nw::render::publish_render_model_static_node_world_transforms(
        *instance, *scene.static_models.front()));

    const glm::vec3 point = mudl::vfx_sequence_resolve_target_point(
        scene,
        0u,
        glm::vec3{0.0f},
        mudl::VfxTargetPointKind::anchor,
        socket_index);

    EXPECT_NEAR(point.x, 12.0f, 1.0e-5f);
    EXPECT_NEAR(point.y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(point.z, 4.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxAnchorWorldPositionUsesCachedSocket)
{
    auto model = make_sequence_model();
    model->nodes.front().world_transform = glm::translate(
        glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f});
    model->sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = model->nodes.front().world_transform,
        .bind_transform = model->nodes.front().world_transform,
        .name = "muzzle",
    });

    const uint32_t socket_index = model->socket_index("muzzle");
    model->sockets.front().name = "renamed_after_binding";
    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    auto* instance = scene.static_model_instance(0u);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(
        glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    ASSERT_TRUE(nw::render::publish_render_model_static_node_world_transforms(
        *instance, *scene.static_models.front()));

    const glm::vec3 point = mudl::vfx_sequence_anchor_world_position(
        scene,
        0u,
        socket_index);

    EXPECT_NEAR(point.x, 11.0f, 1.0e-5f);
    EXPECT_NEAR(point.y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(point.z, 3.0f, 1.0e-5f);
}
