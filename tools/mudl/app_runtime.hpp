#pragma once

#include "mudl_commands.hpp"

#include <nw/render/viewer/camera.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/renderer.hpp>

#include <nw/gfx/gfx.hpp>
#include <nw/render/animation_backend.hpp>
#include <nw/render/nwn/nwn_animation.hpp>
#include <nw/render/shader_provider.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nw {
struct Module;
}

namespace mudl {

static constexpr float kVfxSequenceDefaultDistance = 10.0f;
static constexpr std::string_view kVfxSequenceCasterModel = "c_aribeth";
static constexpr std::string_view kVfxSequenceTargetModel = "c_aribeth";

enum class DragMode {
    none,
    orbit,
    pan,
};

enum class LoadedSceneKind {
    none,
    model,
    vfx_sequence,
    area,
};

struct AppState {
    struct VfxSequencePlaybackStep {
        size_t model_index = 0;
        int start_ms = 0;
        int end_ms = 0;
        glm::vec3 start_pos{0.0f};
        glm::vec3 end_pos{0.0f};
        bool target_side = false;
        VfxSequenceStepKind kind = VfxSequenceStepKind::model;
        bool uses_target_point = false;
        VfxProjectileTransportKind projectile_transport = VfxProjectileTransportKind::none;
        VfxTargetPointKind target_point_kind = VfxTargetPointKind::none;
        VfxProjectilePathKind projectile_path = VfxProjectilePathKind::default_path;
        VfxProjectileOrientationKind projectile_orientation = VfxProjectileOrientationKind::none;
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 authored_axis{0.0f, 0.0f, 1.0f};
        bool has_authored_axis = false;
        std::string caster_animation;
        std::string animation;
        std::string source_anchor;
        std::string target_anchor;
        float scale = 1.0f;
    };

    // Graphics
    nw::gfx::Core* gfx_core = nullptr;
    nw::gfx::Context* gfx_context = nullptr;
    nw::gfx::Handle<nw::gfx::Texture> gltf_ibl_diffuse_texture;
    nw::gfx::BindlessTextureIndex gltf_ibl_diffuse_texture_index = 0;
    nw::gfx::Handle<nw::gfx::Texture> gltf_ibl_specular_texture;
    nw::gfx::BindlessTextureIndex gltf_ibl_specular_texture_index = 0;
    nw::gfx::Handle<nw::gfx::Texture> gltf_brdf_lut_texture;
    nw::gfx::BindlessTextureIndex gltf_brdf_lut_texture_index = 0;
    bool gltf_ibl_enabled = false;

    // Rendering
    std::unique_ptr<nw::render::viewer::Renderer> renderer;
    std::unique_ptr<nw::render::ShaderProvider> shader_provider;
    std::unique_ptr<nw::render::viewer::Camera> camera;

    // Model
    std::unique_ptr<nw::render::viewer::PreviewScene> current_scene;
    std::vector<std::unique_ptr<nw::render::AnimationBackend>> gltf_animation_backends;
    std::vector<nw::render::Pose> gltf_poses;
    std::vector<std::vector<std::string>> model_animation_names;
    std::vector<std::vector<std::string>> gltf_animation_names;
    LoadedSceneKind loaded_scene_kind = LoadedSceneKind::none;
    std::string loaded_scene_source;
    std::optional<VfxSequence> loaded_vfx_sequence;

    // Window
    int window_width = 1280;
    int window_height = 720;
    bool running = true;
    bool enable_validation = false;
    bool show_debug_overlay = false;
    float debug_grid_spacing = 1.0f;
    float debug_grid_major_interval = 10.0f;
    float debug_grid_minor_width = 0.03f;
    float debug_grid_major_width = 0.08f;
    float debug_grid_opacity = 0.65f;
    float debug_grid_z_offset = 0.02f;
    bool renderer_reload_requested = false;
    bool scene_playing = true;

    // Turntable
    bool turntable_mode = false;
    int turntable_frames = 36;
    bool turntable_capture = false;
    int capture_index = 0;
    std::string output_dir = ".";
    std::string screenshot_path;
    bool screenshot_requested = false;
    bool screenshot_captured = false;
    int screenshot_delay_frames = 0;
    std::string animation_override;
    float dangly_scale = 1.0f;
    nw::render::nwn::DanglyMode dangly_mode = nw::render::nwn::DanglyMode::legacy;
    bool area_navigation = false;
    bool area_day_night_autoplay = true;
    float area_day_night_elapsed = 0.0f;
    bool show_authored_area_fog = false;
    uint32_t shadow_debug_mode = 0;
    float gltf_ibl_strength = 1.0f;
    float gltf_exposure = 1.0f;
    bool gltf_animation_autoplay = true;
    uint32_t gltf_animation_clip = 0;
    float gltf_animation_time = 0.0f;
    std::string vfx_sequence_label;
    std::vector<VfxSequencePlaybackStep> vfx_sequence_steps;
    int vfx_sequence_time_ms = 0;
    int vfx_sequence_loop_ms = 0;
    uint32_t vfx_sequence_loop_seed = 0x12345678u;
    size_t vfx_sequence_static_models = 0;
    size_t vfx_sequence_caster_model_index = static_cast<size_t>(-1);
    size_t vfx_sequence_target_model_index = static_cast<size_t>(-1);
    int vfx_sequence_active_step = -1;
    bool vfx_sequence_autoplay = true;

    // Input
    DragMode drag_mode = DragMode::none;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;
};

inline bool is_target_side_sequence_step(const VfxSequenceStep& step, bool source_target_layout)
{
    return source_target_layout && step.target_side;
}

inline std::string_view vfx_target_point_kind_label(VfxTargetPointKind kind)
{
    switch (kind) {
    case VfxTargetPointKind::none: return "none";
    case VfxTargetPointKind::point: return "point";
    case VfxTargetPointKind::root: return "root";
    case VfxTargetPointKind::center: return "center";
    case VfxTargetPointKind::anchor: return "anchor";
    }
    return "unknown";
}

inline glm::mat4 sequence_placement(const glm::vec3& position, const glm::quat& rotation)
{
    return glm::translate(glm::mat4{1.0f}, position) * glm::toMat4(rotation);
}

inline glm::vec3 vfx_sequence_anchor_world_position(const nw::render::viewer::ModelInstance& model, std::string_view anchor)
{
    if (anchor.empty()) {
        return glm::vec3(model.root_transform()[3]);
    }
    if (const auto* node = model.find(anchor)) {
        return glm::vec3(model.root_transform() * node->get_transform()[3]);
    }
    return glm::vec3(model.root_transform()[3]);
}

inline void vfx_sequence_prime_model_animation(nw::render::viewer::ModelInstance& model)
{
    model.anim_cursor_ = 0;
    model.update(0);
}

inline glm::vec3 vfx_sequence_sample_anchor_world_position(
    nw::render::viewer::ModelInstance& model, std::string_view animation, std::string_view anchor)
{
    if (!animation.empty()) {
        model.load_animation(animation);
        vfx_sequence_prime_model_animation(model);
    }
    return vfx_sequence_anchor_world_position(model, anchor);
}

inline bool vfx_sequence_load_preferred_model_animation(
    nw::render::viewer::ModelInstance& model, std::string_view override_animation = {}, std::string_view step_animation = {})
{
    if (!override_animation.empty() && model.load_animation(override_animation)) {
        return true;
    }
    if (!step_animation.empty() && model.load_animation(step_animation)) {
        return true;
    }

    if (model.mdl_) {
        const auto animation = nw::render::viewer::preferred_model_animation_name(
            *model.mdl_, nw::render::viewer::PreferredModelAnimationContext::sequence_effect);
        if (!animation.empty()) {
            return model.load_animation(animation);
        }
    }

    return false;
}

inline std::optional<glm::vec3> vfx_sequence_authored_axis(const nw::render::viewer::PreviewScene& scene, const nw::render::viewer::ModelInstance& model)
{
    glm::vec3 fallback_axis{0.0f};
    bool have_fallback_axis = false;

    for (const auto& particles : scene.particles) {
        if (particles.owner != &model) {
            continue;
        }

        const size_t count = std::min(particles.import.emitter_inits.size(), particles.import.effect.emitters.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& init = particles.import.emitter_inits[i];
            const auto& emitter = particles.import.effect.emitters[i];
            if (emitter.targeting.mode == nw::render::ParticleTargetingMode::none) {
                const auto* emitter_node = init.emitter_node_name.empty()
                    ? model.find(emitter.name)
                    : model.find(init.emitter_node_name);
                glm::vec3 axis{0.0f};
                if (emitter_node) {
                    axis = glm::vec3(emitter_node->get_transform() * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f});
                } else if (init.has_default_transform) {
                    axis = glm::vec3(init.default_transform * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f});
                } else if (init.has_default_orientation) {
                    axis = init.default_orientation * glm::vec3{0.0f, 0.0f, 1.0f};
                }

                if (glm::dot(axis, axis) > 1.0e-6f) {
                    fallback_axis += glm::normalize(axis);
                    have_fallback_axis = true;
                }
                continue;
            }

            std::optional<glm::vec3> source;
            std::optional<glm::vec3> target;
            const auto* emitter_node = init.emitter_node_name.empty()
                ? model.find(emitter.name)
                : model.find(init.emitter_node_name);
            if (emitter_node) {
                source = glm::vec3(emitter_node->get_transform()[3]);
            } else if (init.has_default_position) {
                source = init.default_position;
            }

            if (!init.target_node_name.empty()) {
                if (const auto* target_node = model.find(init.target_node_name)) {
                    target = glm::vec3(target_node->get_transform()[3]);
                }
            } else if (init.has_default_target_offset) {
                target = init.default_target_offset;
            }

            if (!source || !target) {
                continue;
            }

            const glm::vec3 axis = *target - *source;
            if (glm::dot(axis, axis) > 1.0e-6f) {
                return glm::normalize(axis);
            }
        }
    }

    if (have_fallback_axis && glm::dot(fallback_axis, fallback_axis) > 1.0e-6f) {
        return glm::normalize(fallback_axis);
    }

    return std::nullopt;
}

inline float vfx_sequence_step_progress(const AppState::VfxSequencePlaybackStep& step, int time_ms)
{
    const int duration_ms = std::max(step.end_ms - step.start_ms, 1);
    return std::clamp(
        static_cast<float>(time_ms - step.start_ms) / static_cast<float>(duration_ms),
        0.0f, 1.0f);
}

inline glm::vec3 vfx_sequence_perpendicular(const glm::vec3& direction)
{
    glm::vec3 axis = glm::cross(direction, glm::vec3{0.0f, 0.0f, 1.0f});
    if (glm::dot(axis, axis) <= 1.0e-6f) {
        axis = glm::cross(direction, glm::vec3{1.0f, 0.0f, 0.0f});
    }
    const float length_sq = glm::dot(axis, axis);
    if (length_sq <= 1.0e-6f) {
        return glm::vec3{0.0f, 1.0f, 0.0f};
    }
    return axis / std::sqrt(length_sq);
}

inline float vfx_sequence_fract(float value)
{
    return value - std::floor(value);
}

inline float vfx_sequence_hash(float value)
{
    return vfx_sequence_fract(std::sin(value) * 43758.5453f);
}

inline glm::vec3 vfx_sequence_quadratic_bezier(
    const glm::vec3& start, const glm::vec3& control, const glm::vec3& end, float t)
{
    const float inv_t = 1.0f - t;
    return inv_t * inv_t * start + 2.0f * inv_t * t * control + t * t * end;
}

inline glm::vec3 vfx_sequence_projectile_position(
    const AppState::VfxSequencePlaybackStep& step, float t, uint32_t loop_seed)
{
    const glm::vec3 delta = step.end_pos - step.start_pos;
    const float distance = glm::length(delta);
    if (distance <= 1.0e-5f) {
        return step.start_pos;
    }

    const glm::vec3 direction = delta / distance;
    switch (step.projectile_path) {
    case VfxProjectilePathKind::burst_up:
    case VfxProjectilePathKind::linked_burst_up: {
        const glm::vec3 side = vfx_sequence_perpendicular(direction);
        const glm::vec3 bitangent = glm::normalize(glm::cross(direction, side));
        const float seed = static_cast<float>(step.model_index) * 12.9898f
            + static_cast<float>(step.start_ms) * 0.0137f
            + static_cast<float>(loop_seed) * 0.0000137f;
        const float angle = glm::two_pi<float>() * vfx_sequence_hash(seed);
        const float cone_mix = 0.32f + 0.48f * vfx_sequence_hash(seed + 3.17f);
        const glm::vec3 radial = side * std::cos(angle) + bitangent * std::sin(angle);
        const glm::vec3 burst_direction = glm::normalize(direction * (1.0f - cone_mix) + radial * cone_mix);
        const float burst_distance = std::clamp(distance * (0.18f + 0.12f * vfx_sequence_hash(seed + 7.31f)),
            0.35f, 2.2f);
        const glm::vec3 control = step.start_pos + burst_direction * burst_distance;
        return vfx_sequence_quadratic_bezier(step.start_pos, control, step.end_pos, t);
    }
    default:
        return step.start_pos + delta * t;
    }
}

inline glm::vec3 vfx_sequence_projectile_forward(
    const AppState::VfxSequencePlaybackStep& step, float t, uint32_t loop_seed)
{
    constexpr float epsilon = 0.01f;
    const glm::vec3 position = vfx_sequence_projectile_position(step, t, loop_seed);
    if (step.projectile_orientation == VfxProjectileOrientationKind::target) {
        const glm::vec3 target_delta = step.end_pos - position;
        if (glm::dot(target_delta, target_delta) > 1.0e-6f) {
            return glm::normalize(target_delta);
        }
    }

    const float next_t = std::min(t + epsilon, 1.0f);
    const glm::vec3 next_position = vfx_sequence_projectile_position(step, next_t, loop_seed);
    glm::vec3 forward = next_position - position;
    if (glm::dot(forward, forward) <= 1.0e-6f) {
        forward = step.end_pos - step.start_pos;
    }
    if (glm::dot(forward, forward) <= 1.0e-6f) {
        return glm::vec3{0.0f, 0.0f, 1.0f};
    }
    return glm::normalize(forward);
}

inline glm::quat vfx_sequence_projectile_rotation(
    const AppState::VfxSequencePlaybackStep& step, float t, uint32_t loop_seed)
{
    if (!step.has_authored_axis || step.projectile_orientation == VfxProjectileOrientationKind::none) {
        return step.rotation;
    }

    const glm::vec3 desired = vfx_sequence_projectile_forward(step, t, loop_seed);
    if (glm::dot(desired, desired) <= 1.0e-6f) {
        return step.rotation;
    }
    return glm::normalize(glm::rotation(step.authored_axis, desired));
}

inline glm::vec3 vfx_sequence_projectile_root_position(const nw::render::viewer::ModelInstance& model,
    const AppState::VfxSequencePlaybackStep& step, float t, uint32_t loop_seed)
{
    const glm::vec3 source_position = vfx_sequence_projectile_position(step, t, loop_seed);
    const glm::quat rotation = vfx_sequence_projectile_rotation(step, t, loop_seed);
    glm::vec3 local_offset{0.0f};

    if (model.anchor_uses_root_bind_offset) {
        if (!step.source_anchor.empty()) {
            if (const auto* anchor = model.find(step.source_anchor)) {
                local_offset = glm::vec3(anchor->bind_pose_[3]);
            }
        } else if (!model.nodes_.empty()) {
            local_offset = glm::vec3(model.nodes_.front()->bind_pose_[3]);
        }
    } else if (!step.source_anchor.empty()) {
        if (const auto* anchor = model.find(step.source_anchor)) {
            local_offset = glm::vec3(anchor->get_transform()[3]);
        }
    }

    return source_position - (rotation * (local_offset * step.scale));
}

inline VfxProjectileTransportKind vfx_sequence_classify_projectile_transport(
    const nw::render::viewer::PreviewScene& scene, const nw::render::viewer::ModelInstance& model)
{
    for (const auto& scene_particles : scene.particles) {
        if (scene_particles.owner != &model) {
            continue;
        }
        for (const auto& emitter : scene_particles.compiled.effect.emitters) {
            if (emitter.targeting.mode != nw::render::ParticleTargetingMode::none) {
                return VfxProjectileTransportKind::source_rooted_target_point;
            }
        }
    }
    return VfxProjectileTransportKind::moving_root;
}

inline glm::vec3 vfx_sequence_resolve_target_point(const nw::render::viewer::ModelInstance* target, const glm::vec3& fallback,
    VfxTargetPointKind kind, std::string_view anchor = {})
{
    if (!target || kind == VfxTargetPointKind::none || kind == VfxTargetPointKind::point) {
        return fallback;
    }

    switch (kind) {
    case VfxTargetPointKind::root:
        return glm::vec3(target->root_transform()[3]);
    case VfxTargetPointKind::center: {
        const auto bounds = target->current_bounds();
        return 0.5f * (bounds.min + bounds.max);
    }
    case VfxTargetPointKind::anchor:
        if (!anchor.empty()) {
            if (const auto* node = target->find(anchor)) {
                return glm::vec3(target->root_transform() * node->get_transform()[3]);
            }
        }
        {
            const auto bounds = target->current_bounds();
            return 0.5f * (bounds.min + bounds.max);
        }
    case VfxTargetPointKind::none:
    case VfxTargetPointKind::point:
        return fallback;
    }

    return fallback;
}

inline void vfx_sequence_apply_playback_step(nw::render::viewer::PreviewScene& scene, nw::render::viewer::ModelInstance& model,
    const AppState::VfxSequencePlaybackStep& step, uint32_t loop_seed, int time_ms)
{
    model.local_scale_ = step.scale;
    if (step.projectile_transport == VfxProjectileTransportKind::moving_root) {
        const float t = vfx_sequence_step_progress(step, time_ms);
        const glm::quat rotation = vfx_sequence_projectile_rotation(step, t, loop_seed);
        const glm::vec3 position = vfx_sequence_projectile_root_position(model, step, t, loop_seed);
        model.set_placement_transform(sequence_placement(position, rotation));
    } else {
        model.set_placement_transform(sequence_placement(step.start_pos, step.rotation));
    }
    if (step.uses_target_point) {
        scene.set_particle_target_point(&model, step.end_pos);
    }
}

inline void vfx_sequence_clear_state(AppState& state)
{
    state.vfx_sequence_label.clear();
    state.vfx_sequence_steps.clear();
    state.vfx_sequence_time_ms = 0;
    state.vfx_sequence_loop_ms = 0;
    state.vfx_sequence_static_models = 0;
    state.vfx_sequence_caster_model_index = static_cast<size_t>(-1);
    state.vfx_sequence_target_model_index = static_cast<size_t>(-1);
    state.vfx_sequence_active_step = -1;
}

struct VfxSequenceSceneLayout {
    bool use_source_target_layout = false;
    float distance = kVfxSequenceDefaultDistance;
    glm::vec3 source_pos{0.0f};
    glm::vec3 target_root_pos{0.0f};
    nw::render::viewer::ModelInstance* caster = nullptr;
    nw::render::viewer::ModelInstance* target = nullptr;
};

inline VfxSequenceSceneLayout vfx_sequence_prepare_scene(
    AppState& state, nw::render::viewer::PreviewScene& scene, const VfxSequence& sequence, std::string_view animation_override = {})
{
    VfxSequenceSceneLayout layout;
    layout.use_source_target_layout = sequence.source_target_layout;
    layout.distance = sequence.source_target_layout && sequence.source_target_distance > 0.0f
        ? sequence.source_target_distance
        : kVfxSequenceDefaultDistance;
    layout.source_pos = {-0.5f * layout.distance, 0.0f, 0.0f};
    layout.target_root_pos = {0.5f * layout.distance, 0.0f, 0.0f};

    const glm::quat caster_rotation =
        glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::quat target_rotation =
        glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});

    size_t actor_model_count = 0;
    const size_t step_model_limit = scene.models.size();
    state.vfx_sequence_caster_model_index = static_cast<size_t>(-1);
    state.vfx_sequence_target_model_index = static_cast<size_t>(-1);
    if (sequence.use_spell_actors && !scene.models.empty()) {
        layout.caster = scene.models.front().get();
        state.vfx_sequence_caster_model_index = 0;
        layout.caster->set_placement_transform(sequence_placement(layout.source_pos, caster_rotation));
        layout.caster->render_enabled = true;
        actor_model_count = 1;
        if (scene.models.size() >= 2) {
            state.vfx_sequence_target_model_index = 1;
            layout.target = scene.models[state.vfx_sequence_target_model_index].get();
            layout.target->set_placement_transform(sequence_placement(layout.target_root_pos, target_rotation));
            layout.target->render_enabled = true;
            if (layout.target->mdl_) {
                vfx_sequence_load_preferred_model_animation(
                    *layout.target, {},
                    nw::render::viewer::preferred_model_animation_name(*layout.target->mdl_, nw::render::viewer::PreferredModelAnimationContext::hold));
            }
            vfx_sequence_prime_model_animation(*layout.target);
            actor_model_count = 2;
        }
    }
    state.vfx_sequence_static_models = actor_model_count;

    int start_ms = 0;
    int max_end_ms = 0;
    for (size_t i = 0; i < sequence.steps.size() && (i + actor_model_count) < step_model_limit; ++i) {
        auto& model = scene.models[i + actor_model_count];
        model->render_enabled = false;
        const bool is_projectile = sequence.steps[i].kind == VfxSequenceStepKind::projectile;
        const bool target_side = is_target_side_sequence_step(sequence.steps[i], layout.use_source_target_layout);
        const bool anchor_relative =
            !is_projectile && layout.caster && !sequence.steps[i].anchor.empty() && !target_side;
        const bool target_anchor_relative =
            !is_projectile && layout.target && !sequence.steps[i].anchor.empty() && target_side;
        model->local_scale_ = sequence.steps[i].scale;
        model->anchor_position_only = sequence.steps[i].anchor_position_only;
        if (anchor_relative) {
            model->set_transform_anchor(layout.caster, sequence.steps[i].anchor, sequence.steps[i].source_anchor);
        } else if (target_anchor_relative) {
            model->set_transform_anchor(layout.target, sequence.steps[i].anchor, sequence.steps[i].source_anchor);
        }
        vfx_sequence_load_preferred_model_animation(*model, animation_override, sequence.steps[i].animation);
    }

    scene.rebuild_particles();

    for (size_t i = 0; i < sequence.steps.size() && (i + actor_model_count) < step_model_limit; ++i) {
        auto& model = scene.models[i + actor_model_count];
        const bool is_projectile = sequence.steps[i].kind == VfxSequenceStepKind::projectile;
        const bool target_side = is_target_side_sequence_step(sequence.steps[i], layout.use_source_target_layout);
        const bool anchor_relative = layout.caster && !sequence.steps[i].anchor.empty() && !target_side;
        const glm::vec3 resolved_target_pos = vfx_sequence_resolve_target_point(
            layout.target, layout.target_root_pos,
            sequence.steps[i].target_point_kind, sequence.steps[i].target_anchor);
        const glm::vec3 start_pos = (is_projectile
            ? (layout.caster && !sequence.steps[i].anchor.empty()
                      ? vfx_sequence_sample_anchor_world_position(
                            *layout.caster, sequence.steps[i].caster_animation, sequence.steps[i].anchor)
                      : layout.source_pos)
            : (target_side ? resolved_target_pos
                           : (anchor_relative ? glm::vec3{0.0f}
                                              : (layout.use_source_target_layout ? layout.source_pos : glm::vec3{0.0f}))))
            + sequence.steps[i].start_offset;
        const auto projectile_transport = is_projectile
            ? vfx_sequence_classify_projectile_transport(scene, *model)
            : VfxProjectileTransportKind::none;
        const glm::vec3 end_pos =
            ((is_projectile || sequence.steps[i].uses_target_point) ? resolved_target_pos : start_pos)
            + sequence.steps[i].end_offset;

        glm::quat step_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        if (anchor_relative && layout.use_source_target_layout) {
            const glm::vec3 world_forward = glm::normalize(resolved_target_pos - layout.source_pos);
            glm::vec3 desired_local = caster_rotation.w == 1.0f && caster_rotation.x == 0.0f
                    && caster_rotation.y == 0.0f && caster_rotation.z == 0.0f
                ? world_forward
                : glm::inverse(caster_rotation) * world_forward;
            if (glm::dot(desired_local, desired_local) > 1.0e-6f) {
                desired_local = glm::normalize(desired_local);
                if (auto authored_axis = vfx_sequence_authored_axis(scene, *model)) {
                    step_rotation = glm::normalize(glm::rotation(*authored_axis, desired_local));
                }
            }
        }

        const int step_start_ms = sequence.steps[i].start_offset_ms >= 0
            ? sequence.steps[i].start_offset_ms
            : start_ms;
        const int step_end_ms = step_start_ms + std::max(sequence.steps[i].duration_ms, 1);
        glm::vec3 authored_axis{0.0f, 0.0f, 1.0f};
        bool has_authored_axis = false;
        if (is_projectile) {
            if (!sequence.steps[i].source_anchor.empty()) {
                if (const auto* source_anchor = model->find(sequence.steps[i].source_anchor)) {
                    const glm::vec3 root_to_source = glm::vec3(source_anchor->bind_pose_[3]);
                    if (glm::dot(root_to_source, root_to_source) > 1.0e-6f) {
                        authored_axis = glm::normalize(root_to_source);
                        has_authored_axis = true;
                    }
                }
            }
            if (!has_authored_axis) {
                if (auto axis = vfx_sequence_authored_axis(scene, *model)) {
                    authored_axis = glm::normalize(*axis);
                    has_authored_axis = true;
                }
            }
        }

        const glm::vec3 placement_pos =
            projectile_transport == VfxProjectileTransportKind::source_rooted_target_point
            ? start_pos
            : (is_projectile
                ? vfx_sequence_projectile_root_position(*model, {
                      .model_index = i + actor_model_count,
                      .start_ms = step_start_ms,
                      .end_ms = step_end_ms,
                      .start_pos = start_pos,
                      .end_pos = end_pos,
                      .target_side = target_side,
                      .kind = sequence.steps[i].kind,
                      .uses_target_point = sequence.steps[i].uses_target_point,
                      .projectile_transport = VfxProjectileTransportKind::moving_root,
                      .target_point_kind = sequence.steps[i].target_point_kind,
                      .projectile_path = sequence.steps[i].projectile_path,
                      .projectile_orientation = sequence.steps[i].projectile_orientation,
                      .rotation = step_rotation,
                      .authored_axis = authored_axis,
                      .has_authored_axis = has_authored_axis,
                      .caster_animation = sequence.steps[i].caster_animation,
                      .animation = sequence.steps[i].animation,
                      .source_anchor = sequence.steps[i].source_anchor,
                      .target_anchor = sequence.steps[i].target_anchor,
                      .scale = sequence.steps[i].scale,
                  }, 0.0f, state.vfx_sequence_loop_seed)
                : start_pos);
        model->set_placement_transform(sequence_placement(placement_pos, step_rotation));
        state.vfx_sequence_steps.push_back({
            .model_index = i + actor_model_count,
            .start_ms = step_start_ms,
            .end_ms = step_end_ms,
            .start_pos = start_pos,
            .end_pos = end_pos,
            .target_side = target_side,
            .kind = sequence.steps[i].kind,
            .uses_target_point = sequence.steps[i].uses_target_point
                || projectile_transport == VfxProjectileTransportKind::source_rooted_target_point,
            .projectile_transport = projectile_transport,
            .target_point_kind = sequence.steps[i].target_point_kind,
            .projectile_path = sequence.steps[i].projectile_path,
            .projectile_orientation = sequence.steps[i].projectile_orientation,
            .rotation = step_rotation,
            .authored_axis = authored_axis,
            .has_authored_axis = has_authored_axis,
            .caster_animation = sequence.steps[i].caster_animation,
            .animation = sequence.steps[i].animation,
            .source_anchor = sequence.steps[i].source_anchor,
            .target_anchor = sequence.steps[i].target_anchor,
            .scale = sequence.steps[i].scale,
        });
        if (sequence.steps[i].uses_target_point) {
            scene.set_particle_target_point(model.get(), end_pos);
        }
        start_ms = std::max(start_ms, step_end_ms);
        max_end_ms = std::max(max_end_ms, step_end_ms);
    }

    if (layout.caster && !state.vfx_sequence_steps.empty() && !state.vfx_sequence_steps.front().caster_animation.empty()) {
        layout.caster->load_animation(state.vfx_sequence_steps.front().caster_animation);
        vfx_sequence_prime_model_animation(*layout.caster);
    }

    state.vfx_sequence_label = sequence.label;
    state.vfx_sequence_loop_ms = std::max(max_end_ms, 1);
    state.vfx_sequence_loop_seed = 0x12345678u;
    return layout;
}

inline void vfx_sequence_reset_runtime(AppState& state)
{
    state.vfx_sequence_time_ms = 0;
    if (!state.current_scene) {
        return;
    }

    for (auto& model : state.current_scene->models) {
        model->anim_cursor_ = 0;
        model->render_enabled = false;
    }
    for (size_t i = 0; i < state.vfx_sequence_static_models && i < state.current_scene->models.size(); ++i) {
        state.current_scene->models[i]->render_enabled = true;
    }
    state.vfx_sequence_active_step = -1;
    for (const auto& step : state.vfx_sequence_steps) {
        if (step.model_index >= state.current_scene->models.size()) {
            continue;
        }
        auto& model = *state.current_scene->models[step.model_index];
        model.render_enabled = false;
        vfx_sequence_apply_playback_step(*state.current_scene, model, step, state.vfx_sequence_loop_seed, 0);
    }
    state.current_scene->rebuild_particles();
}

inline void vfx_sequence_update_runtime(AppState& state, int32_t dt_ms)
{
    if (!state.current_scene || state.vfx_sequence_steps.empty() || state.vfx_sequence_loop_ms <= 0) {
        return;
    }

    state.vfx_sequence_time_ms += std::max(dt_ms, 0);
    if (state.vfx_sequence_time_ms >= state.vfx_sequence_loop_ms) {
        const int wrapped_time = state.vfx_sequence_time_ms % state.vfx_sequence_loop_ms;
        state.vfx_sequence_loop_seed = state.vfx_sequence_loop_seed * 1664525u + 1013904223u;
        vfx_sequence_reset_runtime(state);
        state.vfx_sequence_time_ms = wrapped_time;
    }

    for (const auto& step : state.vfx_sequence_steps) {
        if (step.model_index >= state.current_scene->models.size()) {
            continue;
        }
        auto& model = *state.current_scene->models[step.model_index];
        model.render_enabled = state.vfx_sequence_time_ms >= step.start_ms
            && state.vfx_sequence_time_ms < step.end_ms;
        vfx_sequence_apply_playback_step(
            *state.current_scene, model, step, state.vfx_sequence_loop_seed, state.vfx_sequence_time_ms);
    }

    int active_step = -1;
    for (size_t i = 0; i < state.vfx_sequence_steps.size(); ++i) {
        const auto& step = state.vfx_sequence_steps[i];
        if (state.vfx_sequence_time_ms >= step.start_ms && state.vfx_sequence_time_ms < step.end_ms) {
            active_step = static_cast<int>(i);
            break;
        }
    }
    if (active_step != state.vfx_sequence_active_step) {
        state.vfx_sequence_active_step = active_step;
        if (active_step >= 0
            && state.vfx_sequence_caster_model_index < state.current_scene->models.size()
            && !state.vfx_sequence_steps[active_step].caster_animation.empty()
            && !state.current_scene->models.empty()) {
            state.current_scene->models[state.vfx_sequence_caster_model_index]->load_animation(
                state.vfx_sequence_steps[active_step].caster_animation);
        }
    }
}

bool init_graphics(AppState& state, SDL_Window* window);
bool init_kernel_services(
    std::string_view module_path = {}, std::string_view user_path = {}, nw::Module** loaded_module = nullptr);
bool init_render_runtime(AppState& state);
bool ensure_gltf_ibl_textures(AppState& state);
bool command_is_headless(std::string_view command);
void shutdown_graphics(AppState& state);
bool reload_renderer_runtime(AppState& state);
void request_renderer_reload(AppState& state);
bool run_compute_smoke(AppState& state);

} // namespace mudl
