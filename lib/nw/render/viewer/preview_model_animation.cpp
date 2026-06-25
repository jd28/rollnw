#include "preview_model_animation.hpp"

#include "preview_scene.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace nw::render::viewer {

namespace {

uint32_t find_render_model_animation_clip(const nw::render::RenderModel& model, std::string_view clip_name) noexcept
{
    if (clip_name.empty()) {
        return std::numeric_limits<uint32_t>::max();
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(model.animations.size()); ++i) {
        if (model.animations[i].name == clip_name) {
            return i;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

bool ensure_render_model_animation_backend(
    nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    size_t model_index)
{
    if (instance.animation.backend) {
        return true;
    }

    auto backend = nw::render::make_render_model_animation_backend(model);
    if (!backend) {
        LOG_F(WARNING, "Failed to build Ozz RenderModel animation backend for model {} instance {}", model.name, model_index);
        return false;
    }
    LOG_F(INFO, "RenderModel animation backend for model {} instance {}: ozz", model.name, model_index);
    instance.animation.backend = std::move(backend);
    return true;
}

} // namespace

std::vector<std::string> collect_render_model_animation_names(const nw::render::RenderModel& model)
{
    std::vector<std::string> result;
    result.reserve(model.animations.size());
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const auto& clip = model.animations[i];
        if (!clip.name.empty()) {
            result.push_back(clip.name);
        } else {
            result.push_back("clip " + std::to_string(i));
        }
    }
    return result;
}

bool supports_render_model_animations(const PreviewScene& scene) noexcept
{
    if (scene.is_area) {
        return false;
    }
    for (const auto& model : scene.static_models) {
        if (model && !model->animations.empty()) {
            return true;
        }
    }
    return false;
}

void rebuild_render_model_animation_instances(PreviewScene& scene, uint32_t clip_index, float time)
{
    time = std::max(0.0f, time);
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        auto* instance = scene.static_model_instance(model_index);
        if (!instance) {
            continue;
        }

        instance->animation = {};
        instance->animation.looping = true;
        instance->animation.clip = clip_index;
        instance->animation.time = time;
        instance->animation.enabled = model && !model->animations.empty() && !model->skeletons.empty();
        if (!model || !instance->animation.enabled) {
            continue;
        }

        ensure_render_model_animation_backend(*instance, *model, model_index);
    }
}

void set_render_model_animation_time(PreviewScene& scene, float time)
{
    time = std::max(0.0f, time);
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }
        if (auto* instance = scene.static_model_instance(model_index)) {
            instance->animation.time = time;
        }
    }
}

void set_render_model_animation_clip(PreviewScene& scene, uint32_t clip_index, float time)
{
    time = std::max(0.0f, time);
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }
        if (auto* instance = scene.static_model_instance(model_index)) {
            instance->animation.clip = clip_index;
            instance->animation.time = time;
            instance->animation.looping = true;
            instance->animation.enabled = !model->skeletons.empty();
        }
    }
}

bool set_render_model_animation_clip_by_name(PreviewScene& scene, std::string_view clip_name, float time)
{
    time = std::max(0.0f, time);
    bool selected = false;
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }

        const uint32_t clip_index = find_render_model_animation_clip(*model, clip_name);
        if (clip_index == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        auto* instance = scene.static_model_instance(model_index);
        if (!instance) {
            continue;
        }

        const bool can_sample_model_animation = !model->skeletons.empty();
        if (can_sample_model_animation && !ensure_render_model_animation_backend(*instance, *model, model_index)) {
            continue;
        }
        instance->animation.clip = clip_index;
        instance->animation.time = time;
        instance->animation.looping = true;
        instance->animation.enabled = can_sample_model_animation;
        selected = true;
    }
    if (selected) {
        scene.rebuild_particles(clip_name);
    }
    return selected;
}

std::string_view find_first_render_model_animation_clip_name(
    const PreviewScene& scene,
    std::span<const std::string_view> clip_names) noexcept
{
    if (scene.is_area) {
        return {};
    }

    for (const auto clip_name : clip_names) {
        if (clip_name.empty()) {
            continue;
        }

        for (const auto& model : scene.static_models) {
            if (!model || model->animations.empty()) {
                continue;
            }
            if (find_render_model_animation_clip(*model, clip_name) != std::numeric_limits<uint32_t>::max()) {
                return clip_name;
            }
        }
    }

    return {};
}

bool set_render_model_animation_clip_by_first_name(
    PreviewScene& scene,
    std::span<const std::string_view> clip_names,
    float time)
{
    const auto clip_name = find_first_render_model_animation_clip_name(scene, clip_names);
    return !clip_name.empty() && set_render_model_animation_clip_by_name(scene, clip_name, time);
}

bool set_default_render_model_animation_clip(
    PreviewScene& scene,
    std::span<const std::string_view> preferred_clip_names,
    float time)
{
    if (set_render_model_animation_clip_by_first_name(scene, preferred_clip_names, time)) {
        return true;
    }

    time = std::max(0.0f, time);
    bool selected = false;
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }

        auto* instance = scene.static_model_instance(model_index);
        if (!instance) {
            continue;
        }

        const bool can_sample_model_animation = !model->skeletons.empty();
        if (can_sample_model_animation && !ensure_render_model_animation_backend(*instance, *model, model_index)) {
            continue;
        }
        instance->animation.clip = 0;
        instance->animation.time = time;
        instance->animation.looping = true;
        instance->animation.enabled = can_sample_model_animation;
        selected = true;
    }
    if (selected) {
        scene.rebuild_particles();
    }
    return selected;
}

void advance_render_model_animation_times(PreviewScene& scene, float dt)
{
    dt = std::max(0.0f, dt);
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }
        if (auto* instance = scene.static_model_instance(model_index)) {
            instance->animation.time += dt;
        }
    }
}

void collect_render_model_animation_samples(
    std::vector<nw::render::ModelInstanceAnimationSample>& out,
    PreviewScene& scene)
{
    out.clear();
    out.reserve(scene.static_models.size());
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model) {
            continue;
        }
        auto* instance = scene.static_model_instance(model_index);
        if (!instance) {
            continue;
        }
        out.push_back(nw::render::ModelInstanceAnimationSample{
            .instance = instance,
            .model = model.get(),
        });
    }
}

nw::render::ModelInstanceAnimationSampleStats sample_render_model_animations(
    std::vector<nw::render::ModelInstanceAnimationSample>& scratch,
    PreviewScene& scene,
    bool allow_reference_fallback)
{
    collect_render_model_animation_samples(scratch, scene);
    return nw::render::sample_model_instance_animations(scratch, allow_reference_fallback);
}

} // namespace nw::render::viewer
