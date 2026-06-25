#include "model_instance_animation.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace nw::render {

namespace {

bool fits_animation_backend_index(size_t count) noexcept
{
    return count <= std::numeric_limits<uint32_t>::max();
}

} // namespace

std::unique_ptr<AnimationBackend> make_render_model_animation_backend(
    const RenderModel& model,
    AnimationBackendKind kind)
{
    auto backend = make_animation_backend(kind);
    if (!backend) {
        return {};
    }

    if (!fits_animation_backend_index(model.skeletons.size())
        || !fits_animation_backend_index(model.animations.size())) {
        return {};
    }

    for (size_t i = 0; i < model.skeletons.size(); ++i) {
        if (!backend->build_skeleton(static_cast<uint32_t>(i), model.skeletons[i])) {
            LOG_F(WARNING,
                "RenderModel '{}' animation skeleton {} failed backend build ({} joints); animation backend disabled",
                model.name,
                i,
                model.skeletons[i].joints.size());
            return {};
        }
    }
    size_t built_clip_count = 0;
    for (size_t i = 0; i < model.animations.size(); ++i) {
        if (backend->build_clip(static_cast<uint32_t>(i), model.animations[i])) {
            ++built_clip_count;
        } else {
            LOG_F(WARNING,
                "RenderModel '{}' animation clip {} '{}' failed backend build; clip will use sample-time fallback policy",
                model.name,
                i,
                model.animations[i].name);
        }
    }
    if (!model.animations.empty() && built_clip_count == 0) {
        LOG_F(WARNING,
            "RenderModel '{}' animation backend built no clips from {} inputs; animation backend disabled",
            model.name,
            model.animations.size());
        return {};
    }
    return backend;
}

bool publish_render_model_static_node_world_transforms(
    ModelInstance& instance,
    const RenderModel& model)
{
    instance.attachment_node_world_transforms.clear();
    instance.attachment_node_transform_valid.clear();
    if (model.nodes.empty()) {
        return false;
    }

    instance.attachment_node_world_transforms.resize(model.nodes.size(), glm::mat4{1.0f});
    instance.attachment_node_transform_valid.assign(model.nodes.size(), 0u);
    for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        instance.attachment_node_world_transforms[node_index] =
            instance.root_transform * model.nodes[node_index].world_transform;
        instance.attachment_node_transform_valid[node_index] = 1u;
    }
    return true;
}

namespace {

enum class SampleResult {
    sampled,
    null_input,
    disabled,
    missing_asset_data,
    invalid_skeleton,
    failed_sample,
};

bool publish_sampled_node_world_transforms(
    ModelInstance& instance,
    const Skeleton& skeleton,
    const Pose& pose)
{
    size_t node_count = 0;
    for (const auto& joint : skeleton.joints) {
        if (joint.node < 0) {
            continue;
        }
        node_count = std::max(node_count, static_cast<size_t>(joint.node) + 1u);
    }

    instance.attachment_node_world_transforms.clear();
    instance.attachment_node_transform_valid.clear();
    if (node_count == 0 || pose.model.empty()) {
        return false;
    }

    instance.attachment_node_world_transforms.resize(node_count, glm::mat4{1.0f});
    instance.attachment_node_transform_valid.assign(node_count, 0u);
    bool published = false;
    const size_t joint_count = std::min(skeleton.joints.size(), pose.model.size());
    for (size_t i = 0; i < joint_count; ++i) {
        const int32_t node = skeleton.joints[i].node;
        if (node < 0) {
            continue;
        }

        const size_t node_index = static_cast<size_t>(node);
        if (node_index >= instance.attachment_node_world_transforms.size()) {
            continue;
        }

        instance.attachment_node_world_transforms[node_index] = instance.root_transform * pose.model[i];
        instance.attachment_node_transform_valid[node_index] = 1u;
        published = true;
    }
    return published;
}

std::vector<int32_t> build_node_to_joint_map(const Skeleton& skeleton)
{
    size_t node_count = 0;
    for (const auto& joint : skeleton.joints) {
        if (joint.node >= 0) {
            node_count = std::max(node_count, static_cast<size_t>(joint.node) + 1u);
        }
    }

    std::vector<int32_t> node_to_joint(node_count, -1);
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const int32_t node = skeleton.joints[i].node;
        if (node >= 0 && static_cast<size_t>(node) < node_to_joint.size()
            && i <= static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            node_to_joint[static_cast<size_t>(node)] = static_cast<int32_t>(i);
        }
    }
    return node_to_joint;
}

bool build_model_instance_skin_matrices(
    const Pose& pose,
    const Skin& skin,
    std::span<const int32_t> node_to_joint,
    std::vector<glm::mat4>& out_skin)
{
    if (skin.joints.empty() || skin.joints.size() > kModelMaxSkinBones
        || skin.joints.size() > skin.inverse_bind_matrices.size()) {
        out_skin.clear();
        return false;
    }

    out_skin.resize(skin.joints.size(), glm::mat4{1.0f});
    for (size_t i = 0; i < skin.joints.size(); ++i) {
        const int32_t node = skin.joints[i];
        if (node < 0 || static_cast<size_t>(node) >= node_to_joint.size()) {
            continue;
        }
        const int32_t joint = node_to_joint[static_cast<size_t>(node)];
        if (joint < 0 || static_cast<size_t>(joint) >= pose.model.size()) {
            continue;
        }
        out_skin[i] = pose.model[static_cast<size_t>(joint)] * skin.inverse_bind_matrices[i];
    }
    return true;
}

SampleResult sample_model_instance_animation_impl(
    ModelInstanceAnimationSample& sample,
    bool allow_reference_fallback)
{
    sample.sampled = false;
    if (!sample.instance || !sample.model) {
        return SampleResult::null_input;
    }

    auto& animation = sample.instance->animation;
    const auto& model = *sample.model;
    animation.skin_matrices.clear();
    if (!animation.enabled) {
        publish_render_model_static_node_world_transforms(*sample.instance, model);
        return SampleResult::disabled;
    }
    if (model.animations.empty() || model.skeletons.empty()) {
        publish_render_model_static_node_world_transforms(*sample.instance, model);
        return SampleResult::missing_asset_data;
    }
    if (!fits_animation_backend_index(model.animations.size())) {
        publish_render_model_static_node_world_transforms(*sample.instance, model);
        return SampleResult::missing_asset_data;
    }

    const uint32_t clip_index = animation.clip % static_cast<uint32_t>(model.animations.size());
    const auto& clip = model.animations[clip_index];
    if (clip.skeleton >= model.skeletons.size()) {
        publish_render_model_static_node_world_transforms(*sample.instance, model);
        return SampleResult::invalid_skeleton;
    }

    const auto& skeleton = model.skeletons[clip.skeleton];
    bool sampled = false;
    if (animation.backend) {
        sampled = animation.backend->sample(clip_index, animation.time, animation.pose, animation.looping);
    }
    if (!sampled && allow_reference_fallback) {
        sampled = sample_clip(skeleton, clip, animation.time, animation.pose, animation.looping);
    }
    if (!sampled) {
        publish_render_model_static_node_world_transforms(*sample.instance, model);
        return SampleResult::failed_sample;
    }

    build_model_matrices(skeleton, animation.pose);
    const bool published_node_transforms = publish_sampled_node_world_transforms(
        *sample.instance,
        skeleton,
        animation.pose);
    animation.skin_matrices.resize(model.skins.size());
    bool published_skin_matrices = false;
    const auto node_to_joint = build_node_to_joint_map(skeleton);
    for (size_t skin_index = 0; skin_index < model.skins.size(); ++skin_index) {
        const auto& skin = model.skins[skin_index];
        if (skin.skeleton != clip.skeleton) {
            continue;
        }
        published_skin_matrices = build_model_instance_skin_matrices(
                                      animation.pose,
                                      skin,
                                      std::span<const int32_t>{node_to_joint.data(), node_to_joint.size()},
                                      animation.skin_matrices[skin_index])
            || published_skin_matrices;
    }
    sample.sampled = published_node_transforms || published_skin_matrices;
    return sample.sampled ? SampleResult::sampled : SampleResult::missing_asset_data;
}

void add_sample_result(ModelInstanceAnimationSampleStats& stats, SampleResult result) noexcept
{
    switch (result) {
    case SampleResult::sampled:
        ++stats.sampled_count;
        break;
    case SampleResult::null_input:
        ++stats.null_input_count;
        break;
    case SampleResult::disabled:
        ++stats.disabled_count;
        break;
    case SampleResult::missing_asset_data:
        ++stats.missing_asset_data_count;
        break;
    case SampleResult::invalid_skeleton:
        ++stats.invalid_skeleton_count;
        break;
    case SampleResult::failed_sample:
        ++stats.failed_sample_count;
        break;
    }
}

} // namespace

ModelInstanceAnimationSampleStats sample_model_instance_animations(
    std::span<ModelInstanceAnimationSample> samples,
    bool allow_reference_fallback)
{
    ModelInstanceAnimationSampleStats stats{};
    stats.input_count = samples.size();
    for (auto& sample : samples) {
        add_sample_result(stats, sample_model_instance_animation_impl(sample, allow_reference_fallback));
    }
    return stats;
}

bool sample_model_instance_animation(
    ModelInstance& instance,
    const RenderModel& model,
    bool allow_reference_fallback)
{
    std::array<ModelInstanceAnimationSample, 1> samples{{
        ModelInstanceAnimationSample{
            .instance = &instance,
            .model = &model,
        },
    }};
    const auto stats = sample_model_instance_animations(samples, allow_reference_fallback);
    return stats.sampled_count == 1;
}

} // namespace nw::render
