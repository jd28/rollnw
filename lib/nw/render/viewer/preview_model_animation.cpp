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

AreaDoorAnimationLease::AreaDoorAnimationLease() = default;
AreaDoorAnimationLease::~AreaDoorAnimationLease() = default;
AreaDoorAnimationLease::AreaDoorAnimationLease(
    AreaDoorAnimationLease&&) noexcept = default;
AreaDoorAnimationLease& AreaDoorAnimationLease::operator=(
    AreaDoorAnimationLease&&) noexcept = default;

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

struct DoorClipSelection {
    std::string_view transition;
    std::string_view hold;
};

bool ensure_render_model_animation_backend(
    nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    size_t model_index);

DoorClipSelection door_clip_selection(
    const AreaDoorAnimationInput& input) noexcept
{
    switch (input.state) {
    case AreaDoorAnimationState::closed:
        return {
            .transition = input.side == 0 ? "closing1"sv : "closing2"sv,
            .hold = "closed"sv,
        };
    case AreaDoorAnimationState::open1:
        return {.transition = "opening1"sv, .hold = "opened1"sv};
    case AreaDoorAnimationState::open2:
        return {.transition = "opening2"sv, .hold = "opened2"sv};
    }
    return {};
}

bool valid_door_animation_input(
    std::span<const AreaDoorAnimationInput> inputs,
    size_t index) noexcept
{
    if (inputs[index].owner.type != nw::ObjectType::door
        || inputs[index].side > 1) {
        return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
        if (inputs[previous].owner == inputs[index].owner) return false;
    }
    return true;
}

const AreaDoorAnimationInput* find_door_animation_input(
    std::span<const AreaDoorAnimationInput> inputs,
    nw::ObjectHandle owner) noexcept
{
    for (size_t index = 0; index < inputs.size(); ++index) {
        if (inputs[index].owner == owner
            && valid_door_animation_input(inputs, index)) {
            return &inputs[index];
        }
    }
    return nullptr;
}

bool select_door_clip(nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    size_t model_index,
    uint32_t clip,
    float time,
    float playback_rate,
    bool looping)
{
    if (clip == std::numeric_limits<uint32_t>::max()
        || clip >= model.animations.size()
        || model.skeletons.empty()
        || !ensure_render_model_animation_backend(instance, model, model_index)) {
        return false;
    }
    instance.scene_animation_enabled = true;
    instance.animation.clip = clip;
    instance.animation.time = time;
    instance.animation.playback_rate = playback_rate;
    instance.animation.looping = looping;
    instance.animation.enabled = true;
    return true;
}

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

AreaDoorAnimationStats update_area_door_animations(
    PreviewScene& scene,
    std::span<const AreaDoorAnimationInput> inputs)
{
    AreaDoorAnimationStats stats{
        .input_count = static_cast<uint32_t>(
            std::min<size_t>(inputs.size(), std::numeric_limits<uint32_t>::max())),
    };
    if (!scene.is_area || inputs.empty()) return stats;

    auto& changed_particle_models = scene.particle_rebuild_model_scratch;
    changed_particle_models.clear();

    for (size_t input_index = 0; input_index < inputs.size(); ++input_index) {
        stats.rejected_input_count
            += !valid_door_animation_input(inputs, input_index);
    }

    // A door can own more than one joined model row. Read the model and
    // instance for the current row inside the loop; no row-zero state is
    // broadcast across a joined door.
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        if (model_index >= scene.static_area_model_info.size()) continue;
        const auto& info = scene.static_area_model_info[model_index];
        if (info.kind != AreaRenderRecordKind::door) continue;

        const auto* input = find_door_animation_input(inputs, info.object);
        if (!input) continue;
        ++stats.matched_model_count;

        if (model_index > std::numeric_limits<uint32_t>::max()) {
            ++stats.missing_clip_count;
            continue;
        }

        const auto& model = scene.static_models[model_index];
        auto* instance = scene.static_model_instance(model_index);
        if (!model || !instance || model->animations.empty()
            || model->skeletons.empty()) {
            ++stats.missing_clip_count;
            continue;
        }

        const auto names = door_clip_selection(*input);
        const uint32_t transition_clip
            = find_render_model_animation_clip(*model, names.transition);
        const uint32_t hold_clip
            = find_render_model_animation_clip(*model, names.hold);

        if (input->phase == AreaDoorAnimationPhase::transition) {
            if (select_door_clip(*instance, *model, model_index,
                    transition_clip, 0.0f, 1.0f, false)) {
                changed_particle_models.push_back(
                    static_cast<uint32_t>(model_index));
                ++stats.changed_model_count;
                continue;
            }
            if (select_door_clip(*instance, *model, model_index,
                    hold_clip, 0.0f, 0.0f, false)) {
                changed_particle_models.push_back(
                    static_cast<uint32_t>(model_index));
                ++stats.changed_model_count;
            } else {
                ++stats.missing_clip_count;
            }
            continue;
        }

        if (instance->animation.enabled
            && instance->animation.clip == transition_clip
            && transition_clip < model->animations.size()) {
            const float duration = model->animations[transition_clip].duration;
            if (std::isfinite(duration) && duration > 0.0f
                && instance->animation.time < duration) {
                continue;
            }
            if (hold_clip == std::numeric_limits<uint32_t>::max()) {
                instance->animation.time = std::max(0.0f, duration);
                instance->animation.playback_rate = 0.0f;
                instance->animation.looping = false;
                ++stats.frozen_transition_count;
                continue;
            }
        }

        if (instance->animation.enabled
            && instance->animation.clip == hold_clip
            && instance->animation.playback_rate == 0.0f) {
            continue;
        }
        if (select_door_clip(*instance, *model, model_index,
                hold_clip, 0.0f, 0.0f, false)) {
            changed_particle_models.push_back(
                static_cast<uint32_t>(model_index));
            ++stats.changed_model_count;
        } else {
            ++stats.missing_clip_count;
        }
    }
    scene.rebuild_model_particles(changed_particle_models);
    changed_particle_models.clear();
    return stats;
}

AreaDoorAnimationStats begin_area_door_animation_lease(
    PreviewScene& scene,
    std::span<const AreaDoorAnimationInput> inputs,
    AreaDoorAnimationLease& lease)
{
    AreaDoorAnimationStats stats{
        .input_count = static_cast<uint32_t>(
            std::min<size_t>(inputs.size(), std::numeric_limits<uint32_t>::max())),
    };
    if (lease.active || !scene.is_area) {
        stats.rejected_input_count = stats.input_count;
        return stats;
    }
    for (size_t input_index = 0; input_index < inputs.size(); ++input_index) {
        stats.rejected_input_count
            += !valid_door_animation_input(inputs, input_index);
    }
    if (stats.rejected_input_count != 0) return stats;

    lease.rows.clear();
    lease.rows.reserve(scene.static_models.size());
    lease.particle_model_indices.clear();
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        if (model_index >= scene.static_area_model_info.size()) continue;
        const auto& info = scene.static_area_model_info[model_index];
        if (info.kind != AreaRenderRecordKind::door
            || !find_door_animation_input(inputs, info.object)) {
            continue;
        }
        auto* instance = scene.static_model_instance(model_index);
        if (!instance) continue;
        if (model_index > std::numeric_limits<uint32_t>::max()) {
            ++stats.missing_clip_count;
            continue;
        }
        lease.particle_model_indices.push_back(
            static_cast<uint32_t>(model_index));
        const auto& animation = instance->animation;
        lease.rows.push_back(AreaDoorAnimationRestoreRow{
            .instance = scene.static_model_instance_handles[model_index],
            .owner = info.object,
            .pose = animation.pose,
            .skin_matrices = animation.skin_matrices,
            .attachment_node_world_transforms
            = instance->attachment_node_world_transforms,
            .attachment_node_transform_valid
            = instance->attachment_node_transform_valid,
            .clip = animation.clip,
            .time = animation.time,
            .playback_rate = animation.playback_rate,
            .looping = animation.looping,
            .enabled = animation.enabled,
            .scene_animation_enabled = instance->scene_animation_enabled,
            .had_backend = static_cast<bool>(animation.backend),
        });
    }

    auto& changed_particle_models = scene.particle_rebuild_model_scratch;
    changed_particle_models.reserve(lease.rows.size());

    lease.particle_positions.clear();
    lease.particles.clear();
    lease.particle_positions.reserve(scene.particles.size());
    lease.particles.reserve(scene.particles.size());
    size_t write_index = 0;
    for (size_t read_index = 0; read_index < scene.particles.size(); ++read_index) {
        auto& particles = scene.particles[read_index];
        if (std::find(lease.particle_model_indices.begin(),
                lease.particle_model_indices.end(),
                particles.owner_model_index)
            != lease.particle_model_indices.end()) {
            lease.particle_positions.push_back(read_index);
            lease.particles.push_back(std::move(particles));
            continue;
        }
        if (write_index != read_index) {
            scene.particles[write_index] = std::move(particles);
        }
        ++write_index;
    }
    scene.particles.erase(
        scene.particles.begin() + static_cast<std::ptrdiff_t>(write_index),
        scene.particles.end());
    scene.rebuild_model_particles(lease.particle_model_indices);

    lease.active = true;
    return update_area_door_animations(scene, inputs);
}

bool restore_area_door_animation_lease(
    PreviewScene& scene,
    AreaDoorAnimationLease& lease) noexcept
{
    if (!lease.active) return true;
    bool restored_all = true;
    for (auto& row : lease.rows) {
        auto* instance = scene.model_instances.get(row.instance);
        if (!instance) {
            restored_all = false;
            continue;
        }
        auto& animation = instance->animation;
        std::swap(animation.pose, row.pose);
        animation.skin_matrices.swap(row.skin_matrices);
        instance->attachment_node_world_transforms.swap(
            row.attachment_node_world_transforms);
        instance->attachment_node_transform_valid.swap(
            row.attachment_node_transform_valid);
        animation.clip = row.clip;
        animation.time = row.time;
        animation.playback_rate = row.playback_rate;
        animation.looping = row.looping;
        animation.enabled = row.enabled;
        instance->scene_animation_enabled = row.scene_animation_enabled;
        if (!row.had_backend) animation.backend.reset();
    }

    scene.particles.erase(
        std::remove_if(scene.particles.begin(), scene.particles.end(),
            [&lease](const SceneParticleSystem& particles) {
                return std::find(lease.particle_model_indices.begin(),
                           lease.particle_model_indices.end(),
                           particles.owner_model_index)
                    != lease.particle_model_indices.end();
            }),
        scene.particles.end());
    bool particle_rows_valid
        = lease.particle_positions.size() == lease.particles.size()
        && lease.particles.size()
            <= scene.particles.capacity() - scene.particles.size();
    size_t restored_particle_count = scene.particles.size();
    for (const size_t position : lease.particle_positions) {
        if (position > restored_particle_count) {
            particle_rows_valid = false;
            break;
        }
        ++restored_particle_count;
    }
    if (!particle_rows_valid) {
        restored_all = false;
    } else {
        for (size_t index = 0; index < lease.particles.size(); ++index) {
            const size_t position = lease.particle_positions[index];
            scene.particles.insert(
                scene.particles.begin()
                    + static_cast<std::ptrdiff_t>(position),
                std::move(lease.particles[index]));
        }
    }
    lease.rows.clear();
    lease.particle_model_indices.clear();
    lease.particle_positions.clear();
    lease.particles.clear();
    lease.active = false;
    return restored_all;
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
