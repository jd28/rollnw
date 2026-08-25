#include "preview_model_animation.hpp"

#include "preview_scene.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace nw::render::viewer {

namespace {

using namespace std::string_view_literals;

constexpr std::array<std::string_view, 8> kPreferredRenderModelHoldAnimations{{
    "cpause1",
    "pause1",
    "closed",
    "opened1",
    "open",
    "default",
    "on",
    "impact",
}};

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

uint32_t find_first_render_model_animation_clip(
    const nw::render::RenderModel& model,
    std::span<const std::string_view> clip_names) noexcept
{
    for (const auto clip_name : clip_names) {
        const uint32_t clip = find_render_model_animation_clip(model, clip_name);
        if (clip != std::numeric_limits<uint32_t>::max()) return clip;
    }
    return std::numeric_limits<uint32_t>::max();
}

struct LocomotionClipSelection {
    uint32_t clip = std::numeric_limits<uint32_t>::max();
    float playback_rate = 1.0f;
};

LocomotionClipSelection select_left_turn_clip(const nw::render::RenderModel& model) noexcept
{
    constexpr std::array left_names{"cturnl"sv, "ccturnl"sv};
    if (const uint32_t clip = find_first_render_model_animation_clip(model, left_names);
        clip != std::numeric_limits<uint32_t>::max()) {
        return {.clip = clip};
    }

    constexpr std::array right_names{"cturnr"sv, "ccturnr"sv};
    return {
        .clip = find_first_render_model_animation_clip(model, right_names),
        .playback_rate = -1.0f,
    };
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

        const bool scene_animation_enabled = instance->scene_animation_enabled;
        instance->animation = {};
        if (!scene_animation_enabled) {
            continue;
        }
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
        if (auto* instance = scene.static_model_instance(model_index);
            instance && instance->scene_animation_enabled) {
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
        if (auto* instance = scene.static_model_instance(model_index);
            instance && instance->scene_animation_enabled) {
            instance->animation.clip = clip_index;
            instance->animation.time = time;
            instance->animation.playback_rate = 1.0f;
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
        if (!instance || !instance->scene_animation_enabled) {
            continue;
        }

        const bool can_sample_model_animation = !model->skeletons.empty();
        if (can_sample_model_animation && !ensure_render_model_animation_backend(*instance, *model, model_index)) {
            continue;
        }
        instance->animation.clip = clip_index;
        instance->animation.time = time;
        instance->animation.playback_rate = 1.0f;
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
    time = std::max(0.0f, time);
    bool selected = false;
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }

        auto* instance = scene.static_model_instance(model_index);
        if (!instance || !instance->scene_animation_enabled) {
            continue;
        }

        uint32_t clip_index = 0;
        for (const auto clip_name : preferred_clip_names) {
            const uint32_t candidate = find_render_model_animation_clip(*model, clip_name);
            if (candidate != std::numeric_limits<uint32_t>::max()) {
                clip_index = candidate;
                break;
            }
        }

        const bool can_sample_model_animation = !model->skeletons.empty();
        if (can_sample_model_animation && !ensure_render_model_animation_backend(*instance, *model, model_index)) {
            continue;
        }
        instance->animation.clip = clip_index;
        instance->animation.time = time;
        instance->animation.playback_rate = 1.0f;
        instance->animation.looping = true;
        instance->animation.enabled = can_sample_model_animation;
        selected = true;
    }
    if (selected) {
        scene.rebuild_particles();
    }
    return selected;
}

bool prime_scene_hold_animation(PreviewScene& scene)
{
    bool loaded_render_model_animation = false;
    if (!scene.hold_animation.empty()) {
        const std::string_view animation = scene.hold_animation;
        for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
            if (auto* instance = scene.static_model_instance(model_index)) {
                const bool scene_animation_enabled = instance->scene_animation_enabled;
                instance->animation = {};
                instance->scene_animation_enabled = scene_animation_enabled;
            }
        }
        loaded_render_model_animation = set_render_model_animation_clip_by_name(scene, animation, 0.0f);
    } else {
        rebuild_render_model_animation_instances(scene, 0, 0.0f);
        loaded_render_model_animation = set_default_render_model_animation_clip(
            scene, kPreferredRenderModelHoldAnimations, 0.0f);
    }
    if (loaded_render_model_animation) {
        advance_render_model_animation_times(scene, 0.033f);
        scene.update(33);
    }
    return loaded_render_model_animation;
}

AreaCreatureLocomotionAnimationStats update_area_creature_locomotion_animations(
    PreviewScene& scene,
    std::span<const AreaCreatureLocomotionAnimationInput> inputs)
{
    AreaCreatureLocomotionAnimationStats stats{
        .input_count = static_cast<uint32_t>(
            std::min<size_t>(inputs.size(), std::numeric_limits<uint32_t>::max())),
    };
    if (!scene.is_area || inputs.empty()) {
        return stats;
    }

    const auto valid_input = [&inputs](size_t index) {
        if (inputs[index].owner.type != nw::ObjectType::creature) {
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (inputs[previous].owner == inputs[index].owner) {
                return false;
            }
        }
        return true;
    };
    for (size_t input_index = 0; input_index < inputs.size(); ++input_index) {
        stats.rejected_input_count += !valid_input(input_index);
    }

    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        if (model_index >= scene.static_area_model_info.size()
            || scene.static_area_model_info[model_index].kind != AreaRenderRecordKind::creature) {
            continue;
        }

        const auto owner = scene.static_area_model_info[model_index].object;
        const AreaCreatureLocomotionAnimationInput* input = nullptr;
        for (size_t input_index = 0; input_index < inputs.size(); ++input_index) {
            if (inputs[input_index].owner == owner && valid_input(input_index)) {
                input = &inputs[input_index];
                break;
            }
        }
        if (!input) {
            continue;
        }
        ++stats.matched_model_count;

        const auto& model = scene.static_models[model_index];
        auto* instance = scene.static_model_instance(model_index);
        if (!model || !instance || !instance->scene_animation_enabled
            || model->animations.empty() || model->skeletons.empty()) {
            ++stats.missing_clip_count;
            continue;
        }

        LocomotionClipSelection selection;
        switch (input->locomotion) {
        case AreaCreatureLocomotion::walking_forward: {
            constexpr std::array names{"walk"sv, "cwalkf"sv, "ccwalkf"sv, "cwalk"sv};
            selection.clip = find_first_render_model_animation_clip(*model, names);
            break;
        }
        case AreaCreatureLocomotion::walking_backward: {
            constexpr std::array names{"cwalkb"sv, "ccwalkb"sv, "walk"sv};
            selection.clip = find_first_render_model_animation_clip(*model, names);
            break;
        }
        case AreaCreatureLocomotion::strafing_left: {
            constexpr std::array names{"cwalkl"sv, "ccwalkl"sv, "walk"sv};
            selection.clip = find_first_render_model_animation_clip(*model, names);
            break;
        }
        case AreaCreatureLocomotion::strafing_right: {
            constexpr std::array names{"cwalkr"sv, "ccwalkr"sv, "walk"sv};
            selection.clip = find_first_render_model_animation_clip(*model, names);
            break;
        }
        case AreaCreatureLocomotion::turning_left: {
            selection = select_left_turn_clip(*model);
            break;
        }
        case AreaCreatureLocomotion::turning_right: {
            constexpr std::array names{"cturnr"sv, "ccturnr"sv};
            selection.clip = find_first_render_model_animation_clip(*model, names);
            break;
        }
        case AreaCreatureLocomotion::idle: {
            constexpr std::array names{"cpause1"sv, "pause1"sv};
            selection.clip = find_first_render_model_animation_clip(*model, names);
            break;
        }
        }
        if (selection.clip == std::numeric_limits<uint32_t>::max()) {
            ++stats.missing_clip_count;
            continue;
        }
        if (instance->animation.enabled
            && instance->animation.clip == selection.clip
            && instance->animation.playback_rate == selection.playback_rate) {
            continue;
        }
        if (!ensure_render_model_animation_backend(*instance, *model, model_index)) {
            ++stats.missing_clip_count;
            continue;
        }
        instance->animation.clip = selection.clip;
        instance->animation.time = selection.playback_rate < 0.0f
            ? model->animations[selection.clip].duration
            : 0.0f;
        instance->animation.playback_rate = selection.playback_rate;
        instance->animation.looping = true;
        instance->animation.enabled = true;
        ++stats.changed_model_count;
    }
    return stats;
}

void advance_render_model_animation_times(PreviewScene& scene, float dt)
{
    dt = std::max(0.0f, dt);
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        const auto& model = scene.static_models[model_index];
        if (!model || model->animations.empty()) {
            continue;
        }
        if (auto* instance = scene.static_model_instance(model_index);
            instance && instance->scene_animation_enabled) {
            auto& animation = instance->animation;
            if (!std::isfinite(animation.playback_rate)) {
                animation.playback_rate = 1.0f;
            }
            animation.time += dt * animation.playback_rate;
            if (animation.playback_rate < 0.0f && animation.looping
                && animation.clip < model->animations.size()) {
                const float duration = model->animations[animation.clip].duration;
                if (duration > 0.0f && animation.time < 0.0f) {
                    animation.time = std::fmod(animation.time, duration) + duration;
                }
            }
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
