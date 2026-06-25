#pragma once

#include <nw/render/animation_backend.hpp>
#include <nw/render/model.hpp>

#include <cstddef>
#include <memory>
#include <span>

namespace nw::render {

struct ModelInstanceAnimationSample {
    ModelInstance* instance = nullptr;
    const RenderModel* model = nullptr;
    bool sampled = false;
};

struct ModelInstanceAnimationSampleStats {
    size_t input_count = 0;
    size_t sampled_count = 0;
    size_t null_input_count = 0;
    size_t disabled_count = 0;
    size_t missing_asset_data_count = 0;
    size_t invalid_skeleton_count = 0;
    size_t failed_sample_count = 0;
};

// Builds an animation backend for all skeletons and clips owned by a RenderModel.
// The production caller should request AnimationBackendKind::ozz. The custom
// backend remains available for reference/debug paths owned by animation_backend.cpp.
std::unique_ptr<AnimationBackend> make_render_model_animation_backend(
    const RenderModel& model,
    AnimationBackendKind kind = AnimationBackendKind::ozz);

// Batch transform from common instance animation state to current pose and skin
// matrices. Clip indices wrap modulo clip count to preserve existing broadcast
// UI semantics. Out-of-range skeletons and failed backend samples are rejected.
//
// The batch records are non-owning and are not retained. This keeps the protocol
// compatible with scene-owned instance/model tables without imposing ownership.
ModelInstanceAnimationSampleStats sample_model_instance_animations(
    std::span<ModelInstanceAnimationSample> samples,
    bool allow_reference_fallback = false);

// Publishes bind/static RenderModel node world rows into the common attachment
// cache. This is the non-animated fallback used by attachment consumers and by
// failed/disabled animation sampling.
bool publish_render_model_static_node_world_transforms(ModelInstance& instance, const RenderModel& model);

// Thin wrapper for callers that only have one instance. New runtime paths should
// prefer the plural batch transform.
bool sample_model_instance_animation(
    ModelInstance& instance,
    const RenderModel& model,
    bool allow_reference_fallback = false);

} // namespace nw::render
