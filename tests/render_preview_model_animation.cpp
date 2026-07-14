#include <nw/render/viewer/preview_model_animation.hpp>
#include <nw/render/viewer/preview_model_draws.hpp>
#include <nw/render/viewer/preview_scene.hpp>

#include <nw/model/Mdl.hpp>
#include <nw/render/nwn/model_loader.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {

std::string resolve_test_data_path(std::string_view path)
{
    const std::array candidates{
        std::filesystem::path{path},
        std::filesystem::path{"tests"} / std::filesystem::path{path},
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return std::string{path};
}

std::unique_ptr<nw::render::RenderModel> make_two_clip_render_model(std::string name)
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

    nw::render::AnimationClip unnamed_clip;
    unnamed_clip.duration = 1.0f;
    unnamed_clip.skeleton = 0;
    unnamed_clip.tracks.resize(1);
    unnamed_clip.tracks[0].translations = {
        {.time = 0.0f, .value = glm::vec3{0.0f, 0.0f, 0.0f}},
        {.time = 1.0f, .value = glm::vec3{0.0f, 4.0f, 0.0f}},
    };

    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = std::move(name);
    model->skeletons.push_back(std::move(skeleton));
    model->animations.push_back(std::move(x_clip));
    model->animations.push_back(std::move(unnamed_clip));
    model->skins.push_back(nw::render::Skin{
        .joints = {0},
        .inverse_bind_matrices = {glm::mat4{1.0f}},
    });
    return model;
}

std::unique_ptr<nw::render::RenderModel> make_render_model_with_invalid_clip(std::string name)
{
    auto model = make_two_clip_render_model(std::move(name));
    if (!model || model->animations.size() < 2) {
        return model;
    }

    auto& invalid_clip = model->animations[1];
    invalid_clip.name = "invalid";
    invalid_clip.duration = 0.0f;
    invalid_clip.tracks[0].translations = {
        {.time = 1.0f, .value = glm::vec3{0.0f, 4.0f, 0.0f}},
    };
    return model;
}

std::unique_ptr<nw::render::RenderModel> make_nwn_animation_render_model(std::string_view path)
{
    nw::model::Mdl mdl{resolve_test_data_path(path)};
    if (!mdl.valid()) {
        return {};
    }

    auto result = nw::render::nwn::import_nwn_model_asset(mdl);
    if (!result.asset) {
        return {};
    }

    const auto& asset = *result.asset;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = asset.name;
    model->materials = asset.materials;
    model->nodes = asset.nodes;
    model->primitives.reserve(asset.primitives.size());
    for (const auto& source : asset.primitives) {
        nw::render::Primitive primitive{};
        primitive.vertex_count = static_cast<uint32_t>(std::min<size_t>(
            source.vertex_count(), std::numeric_limits<uint32_t>::max()));
        primitive.index_count = static_cast<uint32_t>(std::min<size_t>(
            source.index_count(), std::numeric_limits<uint32_t>::max()));
        primitive.material = source.material;
        primitive.skin = source.skin;
        primitive.deformer = source.deformer;
        primitive.node = source.node;
        primitive.skinned = source.skinned;
        primitive.casts_shadow = source.casts_shadow;
        primitive.transform = source.transform;
        primitive.bounds = source.bounds;
        model->primitives.push_back(primitive);
    }
    model->skins = asset.skins;
    model->skeletons = asset.skeletons;
    model->animations = asset.animations;
    model->bounds = asset.bounds;
    model->shadow = asset.shadow;
    return model;
}

std::unique_ptr<nw::render::RenderModel> make_static_socket_render_model(
    std::string name,
    glm::vec3 node_translation,
    std::string socket_name,
    glm::mat4 socket_bind_transform = glm::mat4{1.0f})
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = std::move(name);
    model->nodes.push_back(nw::render::Node{
        .parent = -1,
        .world_transform = glm::translate(glm::mat4{1.0f}, node_translation),
    });
    model->sockets.push_back(nw::render::ModelSocket{
        .source_node_index = 0u,
        .bind_transform = socket_bind_transform,
        .name = std::move(socket_name),
    });
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    return model;
}

float max_abs_matrix_delta(const glm::mat4& lhs, const glm::mat4& rhs) noexcept
{
    float result = 0.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result = std::max(result, std::abs(lhs[column][row] - rhs[column][row]));
        }
    }
    return result;
}

} // namespace

TEST(PreviewModelAnimation, DisabledAnimationPublishesStaticNodeWorldTransforms)
{
    namespace viewer = nw::render::viewer;

    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = "static_nodes";
    model->nodes.push_back(nw::render::Node{
        .parent = -1,
        .world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}),
    });
    model->nodes.push_back(nw::render::Node{
        .parent = 0,
        .world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{4.0f, 5.0f, 6.0f}),
    });

    viewer::PreviewScene scene;
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 20.0f, 30.0f});
    instance->animation.enabled = false;

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);

    EXPECT_EQ(samples.size(), size_t{1});
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.disabled_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{0});
    EXPECT_TRUE(instance->animation.skin_matrices.empty());
    ASSERT_EQ(instance->attachment_node_world_transforms.size(), size_t{2});
    ASSERT_EQ(instance->attachment_node_transform_valid.size(), size_t{2});
    EXPECT_EQ(instance->attachment_node_transform_valid[0], 1u);
    EXPECT_EQ(instance->attachment_node_transform_valid[1], 1u);

    glm::mat4 node0{1.0f};
    glm::mat4 node1{1.0f};
    ASSERT_TRUE(nw::render::model_instance_node_world_transform(*instance, 0, node0));
    ASSERT_TRUE(nw::render::model_instance_node_world_transform(*instance, 1, node1));
    EXPECT_LT(max_abs_matrix_delta(
                  node0,
                  instance->root_transform * scene.static_models[0]->nodes[0].world_transform),
        1.0e-5f);
    EXPECT_LT(max_abs_matrix_delta(
                  node1,
                  instance->root_transform * scene.static_models[0]->nodes[1].world_transform),
        1.0e-5f);
}

TEST(PreviewModelAnimation, RenderModelAttachmentBindingDrivesCommonRoot)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_static_socket_render_model("owner", glm::vec3{2.0f, 3.0f, 4.0f}, "hand"));
    scene.add_attached(
        make_static_socket_render_model(
            "child",
            glm::vec3{0.0f, 0.0f, 0.0f},
            "grip",
            glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f})),
        0u,
        "hand",
        "grip");

    ASSERT_EQ(scene.static_models.size(), size_t{2});
    ASSERT_EQ(scene.model_attachments.size(), size_t{1});
    const auto& binding = scene.model_attachments.front();
    EXPECT_EQ(binding.child_instance_handle, scene.static_model_instance_handles[1]);
    EXPECT_EQ(binding.owner_instance_handle, scene.static_model_instance_handles[0]);
    const auto* bound_child = scene.model_instances.get(binding.child_instance_handle);
    ASSERT_NE(bound_child, nullptr);
    EXPECT_EQ(bound_child->kind, nw::render::ModelInstanceKind::render_model);
    EXPECT_EQ(bound_child->render_model_index, 1u);
    const auto* bound_owner = scene.model_instances.get(binding.owner_instance_handle);
    ASSERT_NE(bound_owner, nullptr);
    EXPECT_EQ(bound_owner->kind, nw::render::ModelInstanceKind::render_model);
    EXPECT_EQ(bound_owner->render_model_index, 0u);
    EXPECT_NE(binding.owner_socket_index, nw::render::kInvalidModelNodeIndex);
    EXPECT_NE(binding.child_source_socket_index, nw::render::kInvalidModelNodeIndex);

    scene.static_models[0]->sockets.front().name = "renamed_owner_after_binding";
    scene.static_models[1]->sockets.front().name = "renamed_child_after_binding";
    const auto sync_stats = viewer::sync_model_instance_runtime_state(scene);
    EXPECT_EQ(sync_stats.render_model_count, 2u);
    EXPECT_EQ(sync_stats.render_model_attachment_binding_count, 1u);
    EXPECT_EQ(sync_stats.render_model_attachment_root_resolved_count, 1u);
    EXPECT_EQ(sync_stats.render_model_attachment_root_failed_count, 0u);

    const auto* child_instance = scene.static_model_instance(1);
    ASSERT_NE(child_instance, nullptr);
    EXPECT_NEAR(child_instance->root_transform[3].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].z, 4.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.min.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.min.y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.min.z, 4.0f, 1.0e-5f);

    glm::mat4 child_node_world{1.0f};
    ASSERT_TRUE(nw::render::model_instance_node_world_transform(*child_instance, 0, child_node_world));
    EXPECT_LT(max_abs_matrix_delta(child_node_world, child_instance->root_transform), 1.0e-5f);
}

TEST(PreviewModelAnimation, RenderModelAttachmentBindingAppliesChildLocalScale)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_static_socket_render_model("owner", glm::vec3{2.0f, 3.0f, 4.0f}, "hand"));
    scene.add_attached(
        make_static_socket_render_model("child", glm::vec3{0.0f, 0.0f, 0.0f}, "grip"),
        0u,
        "hand",
        {},
        2.0f);

    ASSERT_EQ(scene.model_attachments.size(), size_t{1});
    EXPECT_FLOAT_EQ(scene.model_attachments.front().child_local_scale, 2.0f);

    const auto sync_stats = viewer::sync_model_instance_runtime_state(scene);
    EXPECT_EQ(sync_stats.render_model_attachment_binding_count, 1u);
    EXPECT_EQ(sync_stats.render_model_attachment_root_resolved_count, 1u);
    EXPECT_EQ(sync_stats.render_model_attachment_root_failed_count, 0u);

    const auto* child_instance = scene.static_model_instance(1);
    ASSERT_NE(child_instance, nullptr);
    EXPECT_NEAR(child_instance->root_transform[0][0], 2.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[1][1], 2.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[2][2], 2.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->root_transform[3].z, 4.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.min.x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.min.y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.min.z, 4.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.max.x, 4.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.max.y, 5.0f, 1.0e-5f);
    EXPECT_NEAR(child_instance->current_bounds.max.z, 6.0f, 1.0e-5f);
}

TEST(PreviewModelAnimation, RenderModelAttachmentBindingDropsStaleOwnerHandle)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_static_socket_render_model("owner", glm::vec3{2.0f, 3.0f, 4.0f}, "hand"));
    scene.add_attached(make_static_socket_render_model("child", glm::vec3{0.0f, 0.0f, 0.0f}, "grip"),
        0u,
        "hand");

    ASSERT_EQ(scene.model_attachments.size(), size_t{1});
    auto* child_instance = scene.static_model_instance(1);
    ASSERT_NE(child_instance, nullptr);
    child_instance->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{9.0f, 8.0f, 7.0f});
    const glm::mat4 preserved_root = child_instance->root_transform;

    scene.model_instances.destroy(scene.static_model_instance_handles[0]);
    const auto sync_stats = viewer::sync_model_instance_runtime_state(scene);
    EXPECT_EQ(sync_stats.render_model_count, 1u);
    EXPECT_EQ(sync_stats.render_model_attachment_binding_count, 1u);
    EXPECT_EQ(sync_stats.render_model_attachment_root_resolved_count, 0u);
    EXPECT_EQ(sync_stats.render_model_attachment_root_failed_count, 1u);

    child_instance = scene.static_model_instance(1);
    ASSERT_NE(child_instance, nullptr);
    EXPECT_LT(max_abs_matrix_delta(child_instance->root_transform, preserved_root), 1.0e-5f);
    EXPECT_EQ(scene.static_model_instance(0), nullptr);
}

TEST(PreviewModelAnimation, CollectsNamesAndSamplesSceneRenderModelInstances)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_clip_render_model("first"));
    scene.add(make_two_clip_render_model("second"));

    ASSERT_EQ(scene.static_model_instance_handles.size(), 2u);
    ASSERT_TRUE(viewer::supports_render_model_animations(scene));

    const auto names = viewer::collect_render_model_animation_names(*scene.static_models[0]);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "clip 1");

    viewer::rebuild_render_model_animation_instances(scene, 1, -2.0f);
    auto* first = scene.model_instances.get(scene.static_model_instance_handles[0]);
    auto* second = scene.model_instances.get(scene.static_model_instance_handles[1]);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(scene.static_model_instance(0), first);
    EXPECT_EQ(std::as_const(scene).static_model_instance(1), second);
    EXPECT_EQ(scene.static_model_instance(2), nullptr);
    EXPECT_TRUE(first->animation.enabled);
    EXPECT_TRUE(second->animation.enabled);
    EXPECT_TRUE(first->animation.backend);
    EXPECT_TRUE(second->animation.backend);
    EXPECT_EQ(first->animation.clip, 1u);
    EXPECT_EQ(second->animation.clip, 1u);
    EXPECT_FLOAT_EQ(first->animation.time, 0.0f);
    EXPECT_FLOAT_EQ(second->animation.time, 0.0f);

    first->animation.time = 0.25f;
    second->animation.time = 0.5f;
    viewer::advance_render_model_animation_times(scene, 0.25f);
    EXPECT_FLOAT_EQ(first->animation.time, 0.5f);
    EXPECT_FLOAT_EQ(second->animation.time, 0.75f);
    viewer::advance_render_model_animation_times(scene, -1.0f);
    EXPECT_FLOAT_EQ(first->animation.time, 0.5f);
    EXPECT_FLOAT_EQ(second->animation.time, 0.75f);

    viewer::set_render_model_animation_clip(scene, 3, 1.0f);
    EXPECT_EQ(first->animation.clip, 3u);
    EXPECT_EQ(second->animation.clip, 3u);
    EXPECT_FLOAT_EQ(first->animation.time, 1.0f);
    EXPECT_FLOAT_EQ(second->animation.time, 1.0f);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(samples.size(), size_t{2});
    EXPECT_EQ(stats.input_count, size_t{2});
    EXPECT_EQ(stats.sampled_count, size_t{2});
    EXPECT_FALSE(first->animation.skin_matrices.empty());
    EXPECT_FALSE(second->animation.skin_matrices.empty());
}

TEST(PreviewModelAnimation, BootstrapSampleProducesSkinMatricesWithoutTick)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_clip_render_model("session"));

    viewer::rebuild_render_model_animation_instances(scene, 0, 0.0f);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);

    ASSERT_EQ(samples.size(), size_t{1});
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});
    const auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_FALSE(instance->animation.skin_matrices.empty());
}

TEST(PreviewModelAnimation, InvalidRenderModelClipDoesNotDisableValidClip)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_render_model_with_invalid_clip("partial"));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->animation = {};

    ASSERT_TRUE(viewer::set_render_model_animation_clip_by_name(scene, "x", 0.5f));
    ASSERT_TRUE(instance->animation.enabled);
    ASSERT_TRUE(instance->animation.backend);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});

    glm::mat4 node_world{1.0f};
    ASSERT_TRUE(nw::render::model_instance_node_world_transform(*instance, 0, node_world));
    EXPECT_NEAR(node_world[3].x, 1.0f, 5.0e-4f);
}

TEST(PreviewModelAnimation, SelectsRenderModelAnimationByName)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene scene;
    scene.add(make_two_clip_render_model("static"));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->animation = {};

    EXPECT_FALSE(viewer::set_render_model_animation_clip_by_name(scene, "missing", 1.0f));
    EXPECT_TRUE(viewer::set_render_model_animation_clip_by_name(scene, "x", -1.0f));

    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_TRUE(instance->animation.backend);
    EXPECT_EQ(instance->animation.clip, 0u);
    EXPECT_FLOAT_EQ(instance->animation.time, 0.0f);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});
}

TEST(PreviewModelAnimation, SamplesNwnModelAssetAnimationByNameThroughSceneBridge)
{
    namespace viewer = nw::render::viewer;

    auto model = make_nwn_animation_render_model("test_data/user/development/c_bodak.mdl");
    ASSERT_TRUE(model);
    const auto walk_clip = std::find_if(model->animations.begin(), model->animations.end(), [](const auto& clip) {
        return clip.name == "walk";
    });
    ASSERT_NE(walk_clip, model->animations.end());

    viewer::PreviewScene scene;
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->animation = {};

    EXPECT_TRUE(viewer::set_render_model_animation_clip_by_name(scene, "walk", 0.25f));
    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_TRUE(instance->animation.backend);
    EXPECT_FLOAT_EQ(instance->animation.time, 0.25f);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});
    EXPECT_FALSE(instance->attachment_node_world_transforms.empty());
    EXPECT_EQ(instance->attachment_node_world_transforms.size(), instance->attachment_node_transform_valid.size());

    size_t valid_node_transform_count = 0;
    for (const auto valid : instance->attachment_node_transform_valid) {
        valid_node_transform_count += valid != 0u ? 1u : 0u;
    }
    EXPECT_GT(valid_node_transform_count, 0u);

    const auto sampled_primitive = std::find_if(
        scene.static_models.front()->primitives.begin(),
        scene.static_models.front()->primitives.end(),
        [&](const nw::render::Primitive& primitive) {
            glm::mat4 sampled_world{1.0f};
            if (!nw::render::model_instance_node_world_transform(*instance, primitive.node, sampled_world)) {
                return false;
            }
            const glm::mat4 static_world = instance->root_transform * primitive.transform;
            return max_abs_matrix_delta(sampled_world, static_world) > 1.0e-4f;
        });
    ASSERT_NE(sampled_primitive, scene.static_models.front()->primitives.end());

    glm::mat4 sampled_node_world{1.0f};
    ASSERT_TRUE(nw::render::model_instance_node_world_transform(*instance, sampled_primitive->node, sampled_node_world));
    const glm::mat4 draw_world = nw::render::model_instance_primitive_world_transform(
        instance,
        instance->root_transform,
        *sampled_primitive);
    EXPECT_LT(max_abs_matrix_delta(draw_world, sampled_node_world), 1.0e-5f);
}

TEST(PreviewModelAnimation, SamplesNwnSkinnedModelAssetAnimationThroughCommonInstance)
{
    namespace viewer = nw::render::viewer;

    auto model = make_nwn_animation_render_model("test_data/user/development/c_mindflayer.mdl");
    ASSERT_TRUE(model);
    const auto clip_it = std::find_if(model->animations.begin(), model->animations.end(), [](const auto& clip) {
        return clip.name == "cpause1";
    });
    ASSERT_NE(clip_it, model->animations.end());

    const auto skinned_primitive = std::find_if(model->primitives.begin(), model->primitives.end(), [](const auto& primitive) {
        return primitive.skinned;
    });
    ASSERT_NE(skinned_primitive, model->primitives.end());
    ASSERT_LT(skinned_primitive->skin, model->skins.size());
    const uint32_t skin_index = skinned_primitive->skin;

    viewer::PreviewScene scene;
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->animation = {};

    EXPECT_TRUE(viewer::set_render_model_animation_clip_by_name(scene, "cpause1", 0.0f));
    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_TRUE(instance->animation.backend);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    auto stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});
    ASSERT_LT(skin_index, instance->animation.skin_matrices.size());
    ASSERT_FALSE(instance->animation.skin_matrices[skin_index].empty());
    const auto first_matrices = instance->animation.skin_matrices[skin_index];

    viewer::set_render_model_animation_time(scene, 1.13333f);
    stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});
    ASSERT_LT(skin_index, instance->animation.skin_matrices.size());
    ASSERT_EQ(instance->animation.skin_matrices[skin_index].size(), first_matrices.size());

    float max_delta = 0.0f;
    for (size_t i = 0; i < first_matrices.size(); ++i) {
        max_delta = std::max(
            max_delta,
            max_abs_matrix_delta(first_matrices[i], instance->animation.skin_matrices[skin_index][i]));
    }
    EXPECT_GT(max_delta, 1.0e-4f);
}

TEST(PreviewModelAnimation, NwnSkinnedModelAssetPreparedSurfacesUseSampledSkinMatrices)
{
    namespace viewer = nw::render::viewer;

    auto model = make_nwn_animation_render_model("test_data/user/development/c_mindflayer.mdl");
    ASSERT_TRUE(model);
    ASSERT_TRUE(std::any_of(model->primitives.begin(), model->primitives.end(), [](const auto& primitive) {
        return primitive.skinned;
    }));

    viewer::PreviewScene scene;
    scene.add(std::move(model));

    EXPECT_TRUE(viewer::set_render_model_animation_clip_by_name(scene, "cpause1", 0.0f));
    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto sample_stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(sample_stats.input_count, size_t{1});
    EXPECT_EQ(sample_stats.sampled_count, size_t{1});

    viewer::PreviewPreparedModelDraws prepared;
    nw::render::PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);

    EXPECT_GT(surfaces.stats.render_model_draw_count, 0u);
    EXPECT_EQ(surfaces.stats.nwn_legacy_draw_count, 0u);
    const auto& skin_table = surfaces.render_model_skins.stats;
    EXPECT_GT(skin_table.render_model_skinned_surface_count, 0u);
    EXPECT_EQ(skin_table.assigned_surface_count, skin_table.render_model_skinned_surface_count);
    EXPECT_GT(skin_table.table_entry_count, 0u);
    EXPECT_GT(skin_table.matrix_count, 0u);
    EXPECT_EQ(skin_table.bind_pose_fallback_surface_count, 0u);
    EXPECT_EQ(skin_table.invalid_skin_index_count, 0u);
    EXPECT_EQ(skin_table.invalid_matrix_range_count, 0u);

    const auto runtime_report = viewer::build_preview_runtime_model_reports(scene, &surfaces);
    ASSERT_EQ(runtime_report.models.size(), size_t{1});
    EXPECT_GT(runtime_report.models[0].skin_matrix_row_count, size_t{0});
    EXPECT_TRUE(runtime_report.prepared_skin_table_available);
    EXPECT_EQ(runtime_report.prepared_skin_table_stats.assigned_surface_count,
        skin_table.assigned_surface_count);
}

TEST(PreviewModelAnimation, RuntimeReportCountsRenderModelAnimationAndParticles)
{
    namespace viewer = nw::render::viewer;

    auto model = make_two_clip_render_model("runtime_report");
    model->primitives.push_back(nw::render::Primitive{
        .vertex_count = 3,
        .index_count = 3,
        .skin = 0,
        .skinned = true,
        .casts_shadow = true,
    });

    nw::render::ParticleEffectDef effect;
    effect.materials.push_back(nw::render::ParticleMaterialDef{});
    nw::render::ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 8;
    emitter.emission.rate = 8.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    effect.emitters.push_back(std::move(emitter));
    model->particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "runtime_report_fx",
        .animation_name = "x",
        .effect = std::move(effect),
        .effect_events = {{.time = 0.0f, .burst_count = 1u}},
        .animation_length = 1.0f,
    });

    viewer::PreviewScene scene;
    scene.add(std::move(model));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(viewer::set_render_model_animation_clip_by_name(scene, "x", 0.5f));

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    const auto stats = viewer::sample_render_model_animations(samples, scene);
    EXPECT_EQ(stats.input_count, size_t{1});
    EXPECT_EQ(stats.sampled_count, size_t{1});

    const auto report = viewer::build_preview_runtime_model_reports(scene);
    EXPECT_EQ(report.render_model_count, size_t{1});
    EXPECT_EQ(report.skinned_model_count, size_t{1});
    EXPECT_EQ(report.animated_model_count, size_t{1});
    EXPECT_EQ(report.animation_enabled_model_count, size_t{1});
    EXPECT_EQ(report.animation_backend_ready_model_count, size_t{1});
    EXPECT_EQ(report.scene_particle_system_count, size_t{1});
    EXPECT_EQ(report.scene_particle_emitter_count, size_t{1});
    ASSERT_EQ(report.models.size(), size_t{1});

    const auto& runtime = report.models[0];
    EXPECT_EQ(runtime.owner, "runtime_report");
    EXPECT_TRUE(runtime.instance_handle_valid);
    EXPECT_TRUE(runtime.instance_present);
    EXPECT_EQ(runtime.primitive_count, size_t{1});
    EXPECT_EQ(runtime.skinned_primitive_count, size_t{1});
    EXPECT_EQ(runtime.skin_count, size_t{1});
    EXPECT_EQ(runtime.skeleton_count, size_t{1});
    EXPECT_EQ(runtime.animation_count, size_t{2});
    EXPECT_EQ(runtime.selected_clip_index, 0u);
    EXPECT_EQ(runtime.selected_clip_name, "x");
    EXPECT_NEAR(runtime.clip_time, 0.5f, 1.0e-5f);
    EXPECT_TRUE(runtime.animation_enabled);
    EXPECT_TRUE(runtime.animation_backend_ready);
    EXPECT_EQ(runtime.skin_matrix_table_count, size_t{1});
    EXPECT_EQ(runtime.skin_matrix_row_count, size_t{1});
    EXPECT_EQ(runtime.particle_system_count, size_t{1});
    EXPECT_EQ(runtime.scene_particle_system_count, size_t{1});
    EXPECT_EQ(runtime.scene_particle_emitter_count, size_t{1});
    EXPECT_EQ(runtime.scene_particle_max_particles_total, 8u);
    EXPECT_EQ(runtime.scene_particle_event_count, size_t{1});
}

TEST(PreviewModelAnimation, SelectsFirstAvailableRenderModelAnimationName)
{
    namespace viewer = nw::render::viewer;
    using namespace std::string_view_literals;

    viewer::PreviewScene scene;
    scene.add(make_two_clip_render_model("static"));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->animation = {};

    constexpr std::array candidates{"missing"sv, "x"sv};
    EXPECT_EQ(viewer::find_first_render_model_animation_clip_name(scene, candidates), "x"sv);
    EXPECT_TRUE(viewer::set_render_model_animation_clip_by_first_name(scene, candidates, 0.5f));

    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_TRUE(instance->animation.backend);
    EXPECT_EQ(instance->animation.clip, 0u);
    EXPECT_FLOAT_EQ(instance->animation.time, 0.5f);

    constexpr std::array no_matches{"missing"sv, ""sv};
    EXPECT_TRUE(viewer::find_first_render_model_animation_clip_name(scene, no_matches).empty());
    EXPECT_FALSE(viewer::set_render_model_animation_clip_by_first_name(scene, no_matches, 1.0f));
    EXPECT_EQ(instance->animation.clip, 0u);
    EXPECT_FLOAT_EQ(instance->animation.time, 0.5f);
}

TEST(PreviewModelAnimation, DefaultRenderModelAnimationFallsBackToFirstClip)
{
    namespace viewer = nw::render::viewer;
    using namespace std::string_view_literals;

    viewer::PreviewScene scene;
    scene.add(make_two_clip_render_model("static"));

    auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    instance->animation = {};

    constexpr std::array no_matches{"missing"sv, ""sv};
    EXPECT_TRUE(viewer::set_default_render_model_animation_clip(scene, no_matches, 0.75f));

    EXPECT_TRUE(instance->animation.enabled);
    EXPECT_TRUE(instance->animation.backend);
    EXPECT_EQ(instance->animation.clip, 0u);
    EXPECT_FLOAT_EQ(instance->animation.time, 0.75f);
}

TEST(PreviewModelAnimation, SampleCollectionSkipsInvalidSceneOwnedHandles)
{
    namespace viewer = nw::render::viewer;

    viewer::PreviewScene stale_scene;
    stale_scene.add(make_two_clip_render_model("stale"));
    ASSERT_EQ(stale_scene.static_model_instance_handles.size(), 1u);
    stale_scene.model_instances.destroy(stale_scene.static_model_instance_handles[0]);

    std::vector<nw::render::ModelInstanceAnimationSample> samples;
    viewer::collect_render_model_animation_samples(samples, stale_scene);
    EXPECT_TRUE(samples.empty());
    EXPECT_EQ(stale_scene.static_model_instance(0), nullptr);

    viewer::PreviewScene mismatch_scene;
    mismatch_scene.add(make_two_clip_render_model("mismatch"));
    ASSERT_EQ(mismatch_scene.static_model_instance_handles.size(), 1u);
    auto* instance = mismatch_scene.model_instances.get(mismatch_scene.static_model_instance_handles[0]);
    ASSERT_NE(instance, nullptr);
    instance->render_model_index = 99;

    viewer::collect_render_model_animation_samples(samples, mismatch_scene);
    EXPECT_TRUE(samples.empty());
    EXPECT_EQ(mismatch_scene.static_model_instance(0), nullptr);
}
