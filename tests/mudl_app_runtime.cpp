#include "app_runtime.hpp"

#include <gtest/gtest.h>

#include <nw/render/nwn/model_loader.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <utility>

namespace {

std::unique_ptr<nw::render::nwn::ModelInstance> make_sequence_model()
{
    auto model = std::make_unique<nw::render::nwn::ModelInstance>();
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    model->set_placement_transform(glm::mat4{1.0f});
    model->render_enabled = true;
    return model;
}

} // namespace

TEST(MudlAppRuntime, DefaultsToPreparedCommonModelPaths)
{
    const mudl::AppState state{};

    EXPECT_EQ(state.preview_scene_load_options.nwn_model_path, nw::render::viewer::NwnModelPreviewPath::render_model);
}

TEST(MudlAppRuntime, VfxSequenceRuntimeSyncsCommonInstancesAfterSidecarWrites)
{
    mudl::AppState state;
    state.current_scene = std::make_unique<nw::render::viewer::PreviewScene>();
    state.current_scene->add(make_sequence_model());
    ASSERT_EQ(state.current_scene->model_instance_handles.size(), 1u);

    mudl::VfxSequence sequence;
    sequence.label = "test";
    mudl::VfxSequenceStep step;
    step.kind = mudl::VfxSequenceStepKind::model;
    step.duration_ms = 1000;
    step.start_offset = {3.0f, 4.0f, 5.0f};
    sequence.steps.push_back(std::move(step));

    mudl::vfx_sequence_prepare_scene(state, *state.current_scene, sequence);

    auto* instance = state.current_scene->nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->visible);
    EXPECT_NEAR(instance->root_transform[3].x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(instance->root_transform[3].y, 4.0f, 1.0e-5f);
    EXPECT_NEAR(instance->root_transform[3].z, 5.0f, 1.0e-5f);

    state.current_scene->models[0]->render_enabled = true;
    state.current_scene->models[0]->set_placement_transform(
        glm::translate(glm::mat4{1.0f}, glm::vec3{9.0f, 9.0f, 9.0f}));
    mudl::vfx_sequence_reset_runtime(state);

    instance = state.current_scene->nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->visible);
    EXPECT_NEAR(instance->root_transform[3].x, 3.0f, 1.0e-5f);

    mudl::vfx_sequence_update_runtime(state, 500);

    instance = state.current_scene->nwn_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(instance->visible);
    EXPECT_NEAR(instance->root_transform[3].x, 3.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxAuthoredAxisUsesCompiledDefaultsWithoutImportFallback)
{
    auto model = make_sequence_model();

    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    auto* owner = scene.models.front().get();
    ASSERT_NE(owner, nullptr);

    nw::render::viewer::SceneParticleSystem particles;
    particles.owner_model_index = 0u;
    particles.owner_instance_handle = scene.model_instance_handles.front();

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

    const auto axis = mudl::vfx_sequence_authored_axis(scene, *owner);

    ASSERT_TRUE(axis);
    EXPECT_NEAR(axis->x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(axis->y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(axis->z, 1.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxAuthoredAxisUsesCommonAttachmentPoints)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_sequence_model());
    ASSERT_EQ(scene.models.size(), 1u);
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);

    auto* owner = scene.models.front().get();
    auto* common = scene.model_instances.get(scene.model_instance_handles.front());
    ASSERT_NE(common, nullptr);
    common->root_transform = glm::rotate(glm::mat4{1.0f}, 1.57079632679f, glm::vec3{0.0f, 0.0f, 1.0f});
    common->attachment_node_world_transforms.resize(2);
    common->attachment_node_transform_valid.assign(2, 1u);
    common->attachment_node_world_transforms[0] = common->root_transform * glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f});
    common->attachment_node_world_transforms[1] = common->root_transform * glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 2.0f, 3.0f});

    nw::render::viewer::SceneParticleSystem particles;
    particles.owner_model_index = 0u;
    particles.owner_instance_handle = scene.model_instance_handles.front();

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

    const auto axis = mudl::vfx_sequence_authored_axis(scene, *owner);

    ASSERT_TRUE(axis);
    EXPECT_NEAR(axis->x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(axis->y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(axis->z, 0.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxProjectileTransportMatchesParticlesByOwnerHandle)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_sequence_model());
    ASSERT_EQ(scene.models.size(), 1u);
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);

    auto* owner = scene.models.front().get();

    nw::render::viewer::SceneParticleSystem particles;
    particles.owner_model_index = 0u;
    particles.owner_instance_handle = scene.model_instance_handles.front();

    nw::render::CompiledParticleEmitter compiled_emitter;
    compiled_emitter.targeting.mode = nw::render::ParticleTargetingMode::point_gravity;
    particles.compiled.effect.emitters.push_back(std::move(compiled_emitter));

    scene.particles.push_back(std::move(particles));

    EXPECT_EQ(
        mudl::vfx_sequence_classify_projectile_transport(scene, *owner),
        mudl::VfxProjectileTransportKind::source_rooted_target_point);
}

TEST(MudlAppRuntime, VfxProjectileRootPositionUsesCachedSourceSocket)
{
    auto model = make_sequence_model();
    auto source_node = std::make_unique<nw::render::nwn::Node>();
    source_node->bind_pose_ = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    auto* source_node_ptr = source_node.get();
    model->nodes_.push_back(std::move(source_node));
    model->source_nodes_.push_back(source_node_ptr);
    model->sockets_.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = glm::mat4{1.0f},
        .bind_transform = source_node_ptr->bind_pose_,
        .name = "muzzle",
    });

    mudl::AppState::VfxSequencePlaybackStep step;
    step.start_ms = 0;
    step.end_ms = 1000;
    step.start_pos = glm::vec3{10.0f, 0.0f, 0.0f};
    step.end_pos = glm::vec3{20.0f, 0.0f, 0.0f};
    step.has_source_anchor = true;
    step.source_anchor_socket_index = model->socket_index("muzzle");
    model->sockets_.front().name = "renamed_after_binding";

    const glm::vec3 root_position = mudl::vfx_sequence_projectile_root_position(*model, step, 0.0f, 0u);

    EXPECT_NEAR(root_position.x, 8.0f, 1.0e-5f);
    EXPECT_NEAR(root_position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(root_position.z, 0.0f, 1.0e-5f);
}

TEST(MudlAppRuntime, VfxResolveTargetPointUsesCachedTargetSocket)
{
    auto model = make_sequence_model();
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f}));

    auto target_node = std::make_unique<nw::render::nwn::Node>();
    target_node->has_transform_ = true;
    target_node->position_ = glm::vec3{2.0f, 3.0f, 4.0f};
    auto* target_node_ptr = target_node.get();
    model->nodes_.push_back(std::move(target_node));
    model->source_nodes_.push_back(target_node_ptr);
    model->sockets_.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = glm::mat4{1.0f},
        .bind_transform = glm::mat4{1.0f},
        .name = "impact",
    });

    const uint32_t socket_index = model->socket_index("impact");
    model->sockets_.front().name = "renamed_after_binding";

    const glm::vec3 point = mudl::vfx_sequence_resolve_target_point(
        model.get(),
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
    model->set_placement_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f}));

    auto anchor_node = std::make_unique<nw::render::nwn::Node>();
    anchor_node->has_transform_ = true;
    anchor_node->position_ = glm::vec3{1.0f, 2.0f, 3.0f};
    auto* anchor_node_ptr = anchor_node.get();
    model->nodes_.push_back(std::move(anchor_node));
    model->source_nodes_.push_back(anchor_node_ptr);
    model->sockets_.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .local_transform = anchor_node_ptr->get_local_transform(),
        .bind_transform = anchor_node_ptr->bind_pose_,
        .name = "muzzle",
    });

    const uint32_t socket_index = model->socket_index("muzzle");
    model->sockets_.front().name = "renamed_after_binding";

    const glm::vec3 point = mudl::vfx_sequence_anchor_world_position(
        *model,
        socket_index);

    EXPECT_NEAR(point.x, 11.0f, 1.0e-5f);
    EXPECT_NEAR(point.y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(point.z, 3.0f, 1.0e-5f);
}
