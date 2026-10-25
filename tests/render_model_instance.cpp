#include <nw/render/model.hpp>
#include <nw/render/model_instance_animation.hpp>
#include <nw/render/model_instance_attachment.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>
#include <limits>

namespace {

nw::render::RenderModel make_two_clip_render_model()
{
    nw::render::Skeleton skeleton;
    skeleton.joints.push_back(nw::render::Joint{
        .name = "root",
        .parent = -1,
        .node = 0,
        .bind_local = {},
        .root_correction = glm::mat4{1.0f},
        .inverse_bind_matrix = glm::mat4{1.0f},
    });
    nw::render::build_eval_order(skeleton);

    nw::render::AnimationClip x_clip;
    x_clip.name = "x";
    x_clip.duration = 1.0f;
    x_clip.skeleton = 0;
    x_clip.tracks.resize(1);
    x_clip.tracks[0].translations = {
        {.time = 0.0f, .value = glm::vec3{0.0f, 0.0f, 0.0f}},
        {.time = 1.0f, .value = glm::vec3{2.0f, 0.0f, 0.0f}},
    };

    nw::render::AnimationClip y_clip;
    y_clip.name = "y";
    y_clip.duration = 1.0f;
    y_clip.skeleton = 0;
    y_clip.tracks.resize(1);
    y_clip.tracks[0].translations = {
        {.time = 0.0f, .value = glm::vec3{0.0f, 0.0f, 0.0f}},
        {.time = 1.0f, .value = glm::vec3{0.0f, 4.0f, 0.0f}},
    };

    nw::render::RenderModel model;
    model.nodes.push_back(nw::render::Node{});
    model.skeletons.push_back(std::move(skeleton));
    model.animations.push_back(std::move(x_clip));
    model.animations.push_back(std::move(y_clip));
    model.skins.push_back(nw::render::Skin{
        .joints = {0},
        .inverse_bind_matrices = {glm::mat4{1.0f}},
    });
    return model;
}

} // namespace

TEST(RenderModelInstance, HandlesRejectStaleGenerations)
{
    nw::render::ModelInstanceStore store;

    nw::render::ModelInstance first;
    first.render_model_index = 7;
    const auto first_handle = store.create(std::move(first));

    ASSERT_TRUE(store.valid(first_handle));
    ASSERT_NE(store.get(first_handle), nullptr);
    EXPECT_EQ(store.get(first_handle)->render_model_index, 7u);

    store.destroy(first_handle);
    EXPECT_FALSE(store.valid(first_handle));
    EXPECT_EQ(store.get(first_handle), nullptr);

    nw::render::ModelInstance second;
    second.render_model_index = 9;
    const auto second_handle = store.create(std::move(second));

    EXPECT_EQ(second_handle.index, first_handle.index);
    EXPECT_NE(second_handle.generation, first_handle.generation);
    EXPECT_FALSE(store.valid(first_handle));
    ASSERT_TRUE(store.valid(second_handle));
    EXPECT_EQ(store.get(second_handle)->render_model_index, 9u);

    EXPECT_FALSE(store.valid(nw::render::ModelInstanceHandle{}));
    EXPECT_EQ(store.get(nw::render::ModelInstanceHandle{}), nullptr);
}

TEST(RenderModelInstance, MaterialOverrideHandlesRejectStaleGenerations)
{
    nw::render::ModelMaterialOverrideStore store;

    nw::render::ModelMaterialOverride first;
    first.material.albedo = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
    const auto first_handle = store.create(first);

    ASSERT_TRUE(store.valid(first_handle));
    ASSERT_NE(store.get(first_handle), nullptr);
    EXPECT_FLOAT_EQ(store.get(first_handle)->material.albedo.r, 1.0f);

    store.destroy(first_handle);
    EXPECT_FALSE(store.valid(first_handle));
    EXPECT_EQ(store.get(first_handle), nullptr);

    nw::render::ModelMaterialOverride second;
    second.material.albedo = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
    const auto second_handle = store.create(second);

    EXPECT_EQ(second_handle.index, first_handle.index);
    EXPECT_NE(second_handle.generation, first_handle.generation);
    EXPECT_FALSE(store.valid(first_handle));
    ASSERT_TRUE(store.valid(second_handle));
    EXPECT_FLOAT_EQ(store.get(second_handle)->material.albedo.g, 1.0f);

    EXPECT_FALSE(store.valid(nw::render::ModelMaterialOverrideHandle{}));
    EXPECT_EQ(store.get(nw::render::ModelMaterialOverrideHandle{}), nullptr);
}

TEST(RenderModelInstance, RenderModelAttachmentRootTransformUsesSocketRows)
{
    nw::render::RenderModel owner_model;
    owner_model.nodes.resize(1);
    owner_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "hand_r",
    });

    nw::render::RenderModel child_model;
    child_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .bind_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}),
        .name = "root",
    });

    nw::render::ModelInstance owner_instance;
    owner_instance.attachment_node_world_transforms = {
        glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}),
    };
    owner_instance.attachment_node_transform_valid = {1u};

    glm::mat4 root_transform{1.0f};
    EXPECT_TRUE(nw::render::build_model_attachment_root_transform(
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .child_sockets = child_model.sockets,
            .owner_socket_index = 0u,
            .child_source_socket_index = 0u,
        },
        root_transform));
    EXPECT_FLOAT_EQ(root_transform[3].x, 3.0f);
    EXPECT_FLOAT_EQ(root_transform[3].y, 0.0f);
    EXPECT_FLOAT_EQ(root_transform[3].z, 0.0f);
}

TEST(RenderModelInstance, RenderModelAttachmentRootTransformBatchCountsDrops)
{
    nw::render::RenderModel owner_model;
    owner_model.nodes.resize(1);
    owner_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "hand_r",
    });

    nw::render::RenderModel child_model;
    child_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "root",
    });

    nw::render::ModelInstance owner_instance;
    owner_instance.attachment_node_world_transforms = {
        glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}),
    };
    owner_instance.attachment_node_transform_valid = {1u};

    const std::array inputs{
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .child_sockets = child_model.sockets,
            .owner_socket_index = 0u,
            .child_source_socket_index = 99u,
        },
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .child_sockets = child_model.sockets,
            .owner_socket_index = 77u,
            .child_source_socket_index = nw::render::kInvalidModelNodeIndex,
        },
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .child_sockets = child_model.sockets,
            .owner_socket_index = 0u,
            .child_source_socket_index = nw::render::kInvalidModelNodeIndex,
        },
    };
    std::array<nw::render::ModelAttachmentRootTransformOutput, 2> outputs{};

    const auto stats = nw::render::build_model_attachment_root_transforms(inputs, outputs);

    EXPECT_EQ(stats.input_count, 3u);
    EXPECT_EQ(stats.output_count, 2u);
    EXPECT_EQ(stats.output_truncated_count, 1u);
    EXPECT_EQ(stats.resolved_count, 1u);
    EXPECT_EQ(stats.missing_owner_socket_count, 1u);
    EXPECT_EQ(stats.missing_child_socket_count, 1u);
    ASSERT_TRUE(outputs[0].valid);
    EXPECT_FLOAT_EQ(outputs[0].root_transform[3].x, 5.0f);
    EXPECT_FALSE(outputs[1].valid);
}

TEST(RenderModelInstance, AttachmentOrientationPoliciesUseExplicitTransformOrder)
{
    nw::render::RenderModel owner_model;
    owner_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "hand_r",
    });

    nw::render::ModelInstance owner_instance;
    owner_instance.attachment_node_world_transforms = {
        glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f})
            * glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f}),
    };
    owner_instance.attachment_node_transform_valid = {1u};

    const glm::mat4 placement = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    const std::array inputs{
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .owner_socket_index = 0u,
            .child_local_transform = placement,
            .orientation = nw::render::ModelAttachmentOrientationPolicy::socket,
            .source_offset = nw::render::ModelAttachmentSourceOffsetPolicy::none,
        },
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .owner_socket_index = 0u,
            .child_local_transform = placement,
            .orientation = nw::render::ModelAttachmentOrientationPolicy::owner_space_placement,
            .source_offset = nw::render::ModelAttachmentSourceOffsetPolicy::none,
        },
    };
    std::array<nw::render::ModelAttachmentRootTransformOutput, 2> outputs{};

    const auto stats = nw::render::build_model_attachment_root_transforms(inputs, outputs);

    ASSERT_EQ(stats.resolved_count, 2u);
    EXPECT_NEAR(outputs[0].root_transform[3].x, 5.0f, 1.0e-5f);
    EXPECT_NEAR(outputs[0].root_transform[3].y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(outputs[1].root_transform[3].x, 7.0f, 1.0e-5f);
    EXPECT_NEAR(outputs[1].root_transform[3].y, 0.0f, 1.0e-5f);
}

TEST(RenderModelInstance, AttachmentOwnerRootAndSourceOffsetFallbackAreExplicit)
{
    nw::render::RenderModel owner_model;
    owner_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "impact",
    });

    nw::render::ModelInstance owner_instance;
    owner_instance.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f})
        * glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f})
        * glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f});
    owner_instance.attachment_node_world_transforms = {
        glm::translate(glm::mat4{1.0f}, glm::vec3{4.0f, 5.0f, 6.0f}),
    };
    owner_instance.attachment_node_transform_valid = {1u};

    glm::mat4 root_transform{1.0f};
    ASSERT_TRUE(nw::render::build_model_attachment_root_transform(
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .owner_socket_index = 0u,
            .child_local_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}),
            .child_root_bind_translation = glm::vec3{0.0f, 0.0f, 2.0f},
            .orientation = nw::render::ModelAttachmentOrientationPolicy::owner_root,
            .source_offset = nw::render::ModelAttachmentSourceOffsetPolicy::socket_bind_or_root_translation,
        },
        root_transform));
    EXPECT_NEAR(root_transform[3].x, 4.0f, 1.0e-5f);
    EXPECT_NEAR(root_transform[3].y, 6.0f, 1.0e-5f);
    EXPECT_NEAR(root_transform[3].z, 4.0f, 1.0e-5f);
}

TEST(RenderModelInstance, AttachmentBatchRejectsNonInvertibleChildBindTransform)
{
    nw::render::RenderModel owner_model;
    owner_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .name = "hand_r",
    });
    nw::render::RenderModel child_model;
    child_model.sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .bind_transform = glm::mat4{0.0f},
        .name = "grip",
    });
    nw::render::ModelInstance owner_instance;
    owner_instance.attachment_node_world_transforms = {glm::mat4{1.0f}};
    owner_instance.attachment_node_transform_valid = {1u};
    const std::array inputs{
        nw::render::ModelAttachmentRootTransformInput{
            .owner_instance = &owner_instance,
            .owner_sockets = owner_model.sockets,
            .child_sockets = child_model.sockets,
            .owner_socket_index = 0u,
            .child_source_socket_index = 0u,
        },
    };
    std::array<nw::render::ModelAttachmentRootTransformOutput, 1> outputs{};

    const auto stats = nw::render::build_model_attachment_root_transforms(inputs, outputs);

    EXPECT_EQ(stats.resolved_count, 0u);
    EXPECT_EQ(stats.invalid_socket_transform_count, 1u);
    EXPECT_FALSE(outputs[0].valid);
}

TEST(RenderModelInstance, SkinBoneContractCapsPackedJointIndices)
{
    EXPECT_TRUE(nw::render::model_skin_bone_count_supported(nw::render::kModelMaxSkinBones));
    EXPECT_FALSE(nw::render::model_skin_bone_count_supported(nw::render::kModelMaxSkinBones + 1));
    EXPECT_EQ(nw::render::clamp_model_skin_joint_index(0), 0u);
    EXPECT_EQ(nw::render::clamp_model_skin_joint_index(nw::render::kModelMaxSkinBoneIndex),
        nw::render::kModelMaxSkinBoneIndex);
    EXPECT_EQ(nw::render::clamp_model_skin_joint_index(nw::render::kModelMaxSkinBoneIndex + 1),
        nw::render::kModelMaxSkinBoneIndex);
    EXPECT_EQ(nw::render::clamp_model_skin_joint_index(255), nw::render::kModelMaxSkinBoneIndex);
}

TEST(RenderModelInstance, SkinnedPrimitiveWorldTransformStaysAtMeshBindTransform)
{
    nw::render::ModelInstance instance;
    instance.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    instance.attachment_node_world_transforms = {
        glm::translate(glm::mat4{1.0f}, glm::vec3{100.0f, 0.0f, 0.0f}),
    };
    instance.attachment_node_transform_valid = {1u};

    nw::render::Primitive primitive;
    primitive.node = 0;
    primitive.transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    primitive.skinned = true;

    const auto skinned_world = nw::render::model_instance_primitive_world_transform(
        &instance, instance.root_transform, primitive);
    EXPECT_FLOAT_EQ(skinned_world[3].x, 12.0f);

    primitive.skinned = false;
    const auto static_world = nw::render::model_instance_primitive_world_transform(
        &instance, instance.root_transform, primitive);
    EXPECT_FLOAT_EQ(static_world[3].x, 100.0f);
}

TEST(RenderModelInstance, IdentitySkinJointPreservesModelSpaceLane)
{
    auto model = make_two_clip_render_model();
    model.skins[0].joints.push_back(nw::render::kModelSkinIdentityJoint);
    model.skins[0].inverse_bind_matrices.push_back(glm::mat4{1.0f});

    nw::render::ModelInstance instance;
    instance.animation.enabled = true;
    instance.animation.clip = 0;
    instance.animation.time = 0.5f;
    instance.animation.looping = true;
    instance.animation.backend = nw::render::make_render_model_animation_backend(model);
    ASSERT_TRUE(instance.animation.backend);

    ASSERT_TRUE(nw::render::sample_model_instance_animation(instance, model));
    ASSERT_EQ(instance.animation.skin_matrices.size(), 1u);
    ASSERT_EQ(instance.animation.skin_matrices[0].size(), 2u);
    EXPECT_NEAR(instance.animation.skin_matrices[0][0][3].x, 1.0f, 1.0e-3f);
    EXPECT_EQ(instance.animation.skin_matrices[0][1], glm::mat4{1.0f});
}

TEST(RenderModelInstance, RetainedSamplesMatchForcedSamplesAcrossInputChanges)
{
    const auto model = make_two_clip_render_model();
    nw::render::ModelInstance retained;
    nw::render::ModelInstance reference;
    retained.animation.backend = nw::render::make_render_model_animation_backend(model);
    reference.animation.backend = nw::render::make_render_model_animation_backend(model);
    ASSERT_TRUE(retained.animation.backend);
    ASSERT_TRUE(reference.animation.backend);
    std::array samples{nw::render::ModelInstanceAnimationSample{
        .instance = &retained, .model = &model}};

    struct Inputs {
        float time;
        uint32_t clip;
        float x;
        bool enabled;
        bool looping;
        bool reused;
    };
    const Inputs inputs[] = {
        {0.25f, 0u, 0.0f, true, true, false},
        {0.25f, 0u, 0.0f, true, true, true},
        {0.75f, 0u, 0.0f, true, true, false},
        {0.75f, 1u, 0.0f, true, true, false},
        {0.75f, 1u, 10.0f, true, true, false},
        {1.5f, 1u, 10.0f, true, false, false},
        {1.5f, 1u, 10.0f, true, false, true},
        {1.5f, 1u, 10.0f, true, true, false},
        {1.5f, 1u, 10.0f, false, true, false},
        {0.0f, 0u, 10.0f, false, false, true},
        {0.0f, 0u, 20.0f, false, false, false},
        {0.0f, 0u, 20.0f, true, false, false},
        {0.25f, 3u, 20.0f, true, true, false}, // Existing modulo clip policy.
    };
    for (const auto& input : inputs) {
        SCOPED_TRACE(input.time);
        for (auto* instance : {&retained, &reference}) {
            instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{input.x, 0.0f, 0.0f});
            instance->animation.time = input.time;
            instance->animation.clip = input.clip;
            instance->animation.enabled = input.enabled;
            instance->animation.looping = input.looping;
        }
        const auto stats = nw::render::sample_changed_model_instance_animations(samples);
        const bool sampled = nw::render::sample_model_instance_animation(reference, model);
        EXPECT_EQ(stats.reused_count, static_cast<size_t>(input.reused));
        EXPECT_EQ(samples[0].updated, !input.reused);
        EXPECT_EQ(samples[0].sampled, sampled);
        EXPECT_EQ(stats.sampled_count, static_cast<size_t>(input.enabled));
        EXPECT_EQ(stats.disabled_count, static_cast<size_t>(!input.enabled));
        EXPECT_EQ(retained.animation.pose.model, reference.animation.pose.model);
        EXPECT_EQ(retained.animation.skin_matrices, reference.animation.skin_matrices);
        EXPECT_EQ(retained.attachment_node_world_transforms, reference.attachment_node_world_transforms);
        EXPECT_EQ(retained.attachment_node_transform_valid, reference.attachment_node_transform_valid);
    }

    // Forced sampling and explicit bind-pose publication replace the outputs.
    ASSERT_TRUE(nw::render::sample_model_instance_animation(retained, model));
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 0u);
    nw::render::publish_render_model_static_node_world_transforms(retained, model);
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 0u);
    EXPECT_EQ(retained.attachment_node_world_transforms, reference.attachment_node_world_transforms);
    retained.render_model_index = 99u;
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 0u);
}

TEST(RenderModelInstance, OverlayChangesInvalidateRetainedPosesAndPreserveOtherChannels)
{
    using namespace nw::render;
    auto model = make_two_clip_render_model();
    auto& overlay = model.animations[1];
    overlay.tracks[0].translations.clear();
    overlay.tracks[0].rotations = {{0.0f, glm::quat{1, 0, 0, 0}},
        {1.0f, glm::angleAxis(1.0f, glm::vec3{0, 0, 1})}};
    for (const auto kind : {AnimationBackendKind::custom, AnimationBackendKind::ozz}) {
        ModelInstance instance;
        instance.animation.enabled = true;
        instance.animation.time = 0.4f;
        instance.animation.backend = make_render_model_animation_backend(model, kind);
        std::array samples{ModelInstanceAnimationSample{.instance = &instance, .model = &model}};
        ASSERT_EQ(sample_changed_model_instance_animations(samples).sampled_count, 1u);
        const auto base = instance.animation.pose.local[0];
        instance.animation.overlay = {1, 0.3f};
        EXPECT_EQ(sample_changed_model_instance_animations(samples).reused_count, 0u);
        EXPECT_EQ(instance.animation.pose.local[0].translation, base.translation);
        EXPECT_EQ(instance.animation.pose.local[0].scale, base.scale);
        EXPECT_NE(instance.animation.pose.local[0].rotation, base.rotation);
        EXPECT_EQ(sample_changed_model_instance_animations(samples).reused_count, 1u);
        instance.animation.overlay.time = 0.6f;
        EXPECT_EQ(sample_changed_model_instance_animations(samples).reused_count, 0u);
        instance.animation.time = 0.7f;
        EXPECT_EQ(sample_changed_model_instance_animations(samples).reused_count, 0u);
        EXPECT_NE(instance.animation.pose.local[0].translation, base.translation);
        instance.animation.overlay = {};
        EXPECT_EQ(sample_changed_model_instance_animations(samples).reused_count, 0u);
        EXPECT_EQ(instance.animation.pose.local[0].rotation, base.rotation);
        for (const auto bad : {AnimationOverlay{99, 0.0f}, AnimationOverlay{1, -1.0f},
                 AnimationOverlay{1, std::numeric_limits<float>::quiet_NaN()}}) {
            instance.animation.overlay = bad;
            EXPECT_EQ(sample_changed_model_instance_animations(samples).failed_sample_count, 1u);
            EXPECT_EQ(sample_changed_model_instance_animations(samples).reused_count, 0u);
        }
        model.animations[1].skeleton = 1;
        instance.animation.overlay = {1, 0.3f};
        EXPECT_EQ(sample_changed_model_instance_animations(samples).failed_sample_count, 1u);
        model.animations[1].skeleton = 0;
    }
}

TEST(RenderModelInstance, RetainedSamplesRetryFailuresAndRespectInvalidation)
{
    auto model = make_two_clip_render_model();
    nw::render::ModelInstance instance;
    instance.animation.enabled = true;
    instance.animation.time = 0.25f;
    std::array samples{
        nw::render::ModelInstanceAnimationSample{.instance = &instance, .model = &model},
        nw::render::ModelInstanceAnimationSample{},
    };
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto failed = nw::render::sample_changed_model_instance_animations(samples);
        EXPECT_EQ(failed.failed_sample_count, 1u);
        EXPECT_EQ(failed.null_input_count, 1u);
        EXPECT_EQ(failed.reused_count, 0u);
        EXPECT_FALSE(instance.animation.published_inputs.valid);
    }
    const auto recovered = nw::render::sample_changed_model_instance_animations(samples, true);
    EXPECT_EQ(recovered.sampled_count, 1u);
    EXPECT_EQ(recovered.reused_count, 0u);
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples, true).reused_count, 1u);
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples, false).failed_sample_count, 1u);

    // Rare in-place asset edits explicitly invalidate their bound instances.
    instance.animation.enabled = false;
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).disabled_count, 1u);
    model.nodes[0].world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{7.0f, 0.0f, 0.0f});
    instance.animation.published_inputs.valid = false;
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 0u);
    EXPECT_FLOAT_EQ(instance.attachment_node_world_transforms[0][3].x, 7.0f);
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 1u);
}

TEST(RenderModelInstance, RetainedConstantPosesMatchForcedSamplesAcrossTransitions)
{
    for (const auto kind : {nw::render::AnimationBackendKind::custom, nw::render::AnimationBackendKind::ozz}) {
        SCOPED_TRACE(static_cast<int>(kind));
        auto model = make_two_clip_render_model();
        auto& track = model.animations[0].tracks[0];
        track.translations = {{.time = 0.25f, .value = {1.3f, -2.7f, 0.1f}}};
        track.rotations = {{.time = 0.25f, .value = glm::angleAxis(0.37f, glm::normalize(glm::vec3{1.0f, 2.0f, 3.0f}))}};
        track.scales = {{.time = 0.25f, .value = {1.3f, 0.9f, 1.1f}}};
        auto empty = model.animations[0];
        empty.tracks[0] = {};
        model.animations.push_back(empty);
        auto zero_duration = empty;
        zero_duration.duration = 0.0f;
        model.animations.push_back(zero_duration);

        nw::render::ModelInstance retained, reference;
        retained.animation.backend = nw::render::make_render_model_animation_backend(model, kind);
        reference.animation.backend = nw::render::make_render_model_animation_backend(model, kind);
        ASSERT_TRUE(retained.animation.backend);
        ASSERT_TRUE(reference.animation.backend);
        std::array samples{nw::render::ModelInstanceAnimationSample{.instance = &retained, .model = &model}};
        const auto compare = [&](bool reused, bool fallback) {
            const auto stats = nw::render::sample_changed_model_instance_animations(samples, fallback);
            EXPECT_EQ(samples[0].sampled, nw::render::sample_model_instance_animation(reference, model, fallback));
            EXPECT_EQ(stats.reused_count, static_cast<size_t>(reused));
            EXPECT_EQ(samples[0].updated, !reused);
            EXPECT_EQ(retained.animation.pose.model, reference.animation.pose.model);
            EXPECT_EQ(retained.animation.skin_matrices, reference.animation.skin_matrices);
            EXPECT_EQ(retained.attachment_node_world_transforms, reference.attachment_node_world_transforms);
            EXPECT_EQ(retained.attachment_node_transform_valid, reference.attachment_node_transform_valid);
        };
        struct Inputs {
            float time;
            uint32_t clip;
            float x;
            bool enabled;
            bool looping;
            bool fallback;
            bool reused;
        };
        const Inputs inputs[] = {
            {0.1f, 0, 0, true, true, false, false},
            {0.2f, 0, 0, true, true, false, true},
            {-0.1f, 0, 0, true, true, false, true},
            {1000.3f, 0, 0, true, true, false, true},
            {0.3f, 0, 0, true, false, false, false},
            {0.4f, 0, 7, true, false, false, false},
            {0.5f, 0, 7, true, false, false, true},
            {0.6f, 0, 7, true, false, true, false},
            {0.7f, 0, 7, false, false, true, false},
            {0.8f, 0, 7, true, false, true, false},
            {0.9f, 0, 7, true, false, true, true},
            {0.1f, 2, 7, true, true, false, false},
            {0.2f, 2, 7, true, true, false, true},
            {0.3f, 1, 7, true, true, false, false},
            {0.4f, 1, 7, true, true, false, false},
            {0.5f, 3, 7, true, true, false, false},
            {0.6f, 3, 7, true, true, false, true},
            {0.7f, 4, 7, true, true, false, false}, // Modulo clip selection.
            {0.8f, 4, 7, true, true, false, true},
        };
        for (const auto& input : inputs) {
            SCOPED_TRACE(input.time);
            for (auto* instance : {&retained, &reference}) {
                instance->animation.time = input.time;
                instance->animation.clip = input.clip;
                instance->animation.enabled = input.enabled;
                instance->animation.looping = input.looping;
                instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{input.x, 0.0f, 0.0f});
            }
            compare(input.reused, input.fallback);
        }

        // A forced publication invalidates the retained classification too.
        ASSERT_TRUE(nw::render::sample_model_instance_animation(retained, model));
        compare(false, false);

        // An asset edit can turn a constant channel into an interpolated one.
        auto& keys = model.animations[0].tracks[0].translations;
        keys.push_back({.time = 1.0f, .value = keys.front().value});
        for (auto* instance : {&retained, &reference}) {
            instance->animation.backend = nw::render::make_render_model_animation_backend(model, kind);
            ASSERT_TRUE(instance->animation.backend);
            instance->animation.published_inputs.valid = false;
        }
        compare(false, false);
        for (const float time : {0.3f, 0.4f}) {
            retained.animation.time = reference.animation.time = time;
            compare(false, false);
        }
    }
}

TEST(RenderModelInstance, ConstantPoseReusePreservesNonfiniteTimesAndFailureRetries)
{
    auto model = make_two_clip_render_model();
    model.animations[0].tracks[0].translations.resize(1);
    nw::render::ModelInstance instance;
    instance.animation.enabled = true;
    std::array samples{nw::render::ModelInstanceAnimationSample{.instance = &instance, .model = &model}};
    // Exercise reference fallback, which returns the authored single key even
    // for nonfinite time. Time independence must not create new nonfinite hits.
    for (const float time : {0.0f, std::numeric_limits<float>::infinity(), 0.1f,
             -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 0.2f}) {
        instance.animation.time = time;
        const auto stats = nw::render::sample_changed_model_instance_animations(samples, true);
        EXPECT_EQ(stats.sampled_count, 1u);
        EXPECT_EQ(stats.reused_count, 0u);
    }
    instance.animation.time = 0.3f;
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples, true).reused_count, 1u);
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto stats = nw::render::sample_changed_model_instance_animations(samples, false);
        EXPECT_EQ(stats.failed_sample_count, 1u);
        EXPECT_EQ(stats.reused_count, 0u);
    }
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples, true).reused_count, 0u);
}

TEST(RenderModelInstance, SharedAssetInstancesHoldIndependentGltfAnimationState)
{
    const auto model = make_two_clip_render_model();

    nw::render::ModelInstance first;
    first.render_model_index = 0;
    first.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    first.animation.enabled = true;
    first.animation.clip = 0;
    first.animation.time = 0.25f;
    first.animation.looping = true;
    first.animation.backend = nw::render::make_render_model_animation_backend(model);
    ASSERT_TRUE(first.animation.backend);

    nw::render::ModelInstance second;
    second.render_model_index = 0;
    second.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{20.0f, 0.0f, 0.0f});
    second.animation.enabled = true;
    second.animation.clip = 1;
    second.animation.time = 0.75f;
    second.animation.looping = true;
    second.animation.backend = nw::render::make_render_model_animation_backend(model);
    ASSERT_TRUE(second.animation.backend);

    std::array<nw::render::ModelInstanceAnimationSample, 2> samples{{
        {
            .instance = &first,
            .model = &model,
        },
        {
            .instance = &second,
            .model = &model,
        },
    }};
    const auto stats = nw::render::sample_model_instance_animations(samples);

    EXPECT_EQ(stats.input_count, size_t{2});
    EXPECT_EQ(stats.sampled_count, size_t{2});
    EXPECT_EQ(stats.failed_sample_count, size_t{0});
    EXPECT_TRUE(samples[0].sampled);
    EXPECT_TRUE(samples[1].sampled);

    EXPECT_EQ(first.animation.clip, 0u);
    EXPECT_FLOAT_EQ(first.animation.time, 0.25f);
    EXPECT_EQ(second.animation.clip, 1u);
    EXPECT_FLOAT_EQ(second.animation.time, 0.75f);
    EXPECT_NEAR(first.animation.pose.local[0].translation.x, 0.5f, 1.0e-3f);
    EXPECT_NEAR(first.animation.pose.local[0].translation.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(second.animation.pose.local[0].translation.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(second.animation.pose.local[0].translation.y, 3.0f, 1.0e-3f);
    EXPECT_EQ(first.animation.skin_matrices.size(), 1u);
    EXPECT_EQ(second.animation.skin_matrices.size(), 1u);
    ASSERT_EQ(first.attachment_node_world_transforms.size(), 1u);
    ASSERT_EQ(second.attachment_node_world_transforms.size(), 1u);
    ASSERT_EQ(first.attachment_node_transform_valid.size(), 1u);
    ASSERT_EQ(second.attachment_node_transform_valid.size(), 1u);
    EXPECT_EQ(first.attachment_node_transform_valid[0], 1u);
    EXPECT_EQ(second.attachment_node_transform_valid[0], 1u);
    EXPECT_NEAR(first.attachment_node_world_transforms[0][3].x, 10.5f, 1.0e-3f);
    EXPECT_NEAR(first.attachment_node_world_transforms[0][3].y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(second.attachment_node_world_transforms[0][3].x, 20.0f, 1.0e-3f);
    EXPECT_NEAR(second.attachment_node_world_transforms[0][3].y, 3.0f, 1.0e-3f);

    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 0u);
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 2u);
    first.animation.time = 0.5f;
    EXPECT_EQ(nw::render::sample_changed_model_instance_animations(samples).reused_count, 1u);
    EXPECT_TRUE(samples[0].updated);
    EXPECT_FALSE(samples[1].updated);
    EXPECT_NEAR(first.attachment_node_world_transforms[0][3].x, 11.0f, 1.0e-3f);
    EXPECT_NEAR(second.attachment_node_world_transforms[0][3].y, 3.0f, 1.0e-3f);
}

TEST(RenderModelInstance, SharedSkeletonSamplesMultipleSkinTables)
{
    auto model = make_two_clip_render_model();
    model.skins.push_back(nw::render::Skin{
        .skeleton = 0,
        .joints = {0},
        .inverse_bind_matrices = {glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 2.0f, 0.0f})},
    });

    nw::render::ModelInstance instance;
    instance.animation.enabled = true;
    instance.animation.clip = 0;
    instance.animation.time = 0.5f;
    instance.animation.looping = true;
    instance.animation.backend = nw::render::make_render_model_animation_backend(model);
    ASSERT_TRUE(instance.animation.backend);

    ASSERT_TRUE(nw::render::sample_model_instance_animation(instance, model));
    ASSERT_EQ(instance.animation.skin_matrices.size(), 2u);
    ASSERT_EQ(instance.animation.skin_matrices[0].size(), 1u);
    ASSERT_EQ(instance.animation.skin_matrices[1].size(), 1u);
    EXPECT_NEAR(instance.animation.skin_matrices[0][0][3].x, 1.0f, 1.0e-3f);
    EXPECT_NEAR(instance.animation.skin_matrices[1][0][3].x, 1.0f, 1.0e-3f);
    EXPECT_NEAR(instance.animation.skin_matrices[1][0][3].y, 2.0f, 1.0e-3f);
}
