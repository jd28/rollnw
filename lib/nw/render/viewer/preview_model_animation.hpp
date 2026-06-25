#pragma once

#include <nw/render/model_instance_animation.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

namespace nw::render::viewer {

struct PreviewScene;

// Returns UI-facing clip labels for a RenderModel asset. Empty clip names are
// reported as stable "clip N" labels.
std::vector<std::string> collect_render_model_animation_names(const nw::render::RenderModel& model);

// True when any scene RenderModel has imported animation clips. This is a scene
// query only; individual instances may still be disabled if their skeleton data
// is missing.
bool supports_render_model_animations(const PreviewScene& scene) noexcept;

// Batch transforms over scene-owned RenderModel common instances. Missing,
// stale, or mismatched handles are skipped. Time and dt inputs clamp to zero;
// clip indices are stored as provided and wrap during sampling.
void rebuild_render_model_animation_instances(PreviewScene& scene, uint32_t clip_index, float time);
void set_render_model_animation_time(PreviewScene& scene, float time);
void set_render_model_animation_clip(PreviewScene& scene, uint32_t clip_index, float time);
bool set_render_model_animation_clip_by_name(PreviewScene& scene, std::string_view clip_name, float time);
std::string_view find_first_render_model_animation_clip_name(
    const PreviewScene& scene,
    std::span<const std::string_view> clip_names) noexcept;
bool set_render_model_animation_clip_by_first_name(
    PreviewScene& scene,
    std::span<const std::string_view> clip_names,
    float time);
// Preview startup selection: try preferred names, then fall back to clip 0 for
// animated RenderModels whose source naming does not match the preferred list.
bool set_default_render_model_animation_clip(
    PreviewScene& scene,
    std::span<const std::string_view> preferred_clip_names,
    float time);
void advance_render_model_animation_times(PreviewScene& scene, float dt);
void collect_render_model_animation_samples(
    std::vector<nw::render::ModelInstanceAnimationSample>& out,
    PreviewScene& scene);
nw::render::ModelInstanceAnimationSampleStats sample_render_model_animations(
    std::vector<nw::render::ModelInstanceAnimationSample>& scratch,
    PreviewScene& scene,
    bool allow_reference_fallback = false);

} // namespace nw::render::viewer
