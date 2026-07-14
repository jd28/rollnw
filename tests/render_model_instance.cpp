#include <nw/render/model.hpp>
#include <nw/render/model_instance_animation.hpp>
#include <nw/render/model_instance_attachment.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>

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
    first.kind = nw::render::ModelInstanceKind::render_model;
    first.render_model_index = 7;
    const auto first_handle = store.create(std::move(first));

    ASSERT_TRUE(store.valid(first_handle));
    ASSERT_NE(store.get(first_handle), nullptr);
    EXPECT_EQ(store.get(first_handle)->render_model_index, 7u);

    store.destroy(first_handle);
    EXPECT_FALSE(store.valid(first_handle));
    EXPECT_EQ(store.get(first_handle), nullptr);

    nw::render::ModelInstance second;
    second.kind = nw::render::ModelInstanceKind::render_model;
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

TEST(RenderModelInstance, SharedAssetInstancesHoldIndependentGltfAnimationState)
{
    const auto model = make_two_clip_render_model();

    nw::render::ModelInstance first;
    first.kind = nw::render::ModelInstanceKind::render_model;
    first.render_model_index = 0;
    first.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    first.animation.enabled = true;
    first.animation.clip = 0;
    first.animation.time = 0.25f;
    first.animation.looping = true;
    first.animation.backend = nw::render::make_render_model_animation_backend(model);
    ASSERT_TRUE(first.animation.backend);

    nw::render::ModelInstance second;
    second.kind = nw::render::ModelInstanceKind::render_model;
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
    instance.kind = nw::render::ModelInstanceKind::render_model;
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
