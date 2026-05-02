#include "viewer_runtime.hpp"

#include "imgui_runtime.hpp"
#include "preview_scene.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Module.hpp>
#include <nw/util/game_install.hpp>

#include <SDL3/SDL.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace mudl {
namespace {

static constexpr float kAreaViewerDayNightCycleSeconds = 60.0f;
static constexpr float kAreaViewerTwilightHoldStrength = 0.72f;
static constexpr float kAreaViewerScrubStepSeconds = 3.0f;
static constexpr int32_t kVfxSequenceStepMs = 33;
static constexpr uint32_t kShadowMapResolution = 2048;
static constexpr float kShadowMaxDistance = 120.0f;
bool should_tick_area_day_night(const PreviewScene& scene);
void fit_area_navigation_camera(Camera& camera, const Bounds& bounds);

void fit_static_model_camera(Camera& camera, const Bounds& bounds)
{
    const float radius = std::max(bounds.radius(), 1.0f);
    camera.set_orbit_view(bounds.center(), radius * 2.25f, -90.0f, 12.0f, Camera::ProjectionMode::perspective);
}

glm::vec3 srgb8_to_linear(uint32_t packed)
{
    const auto decode = [](uint8_t channel) {
        return std::pow(static_cast<float>(channel) / 255.0f, 2.2f);
    };
    return glm::vec3{
        decode(static_cast<uint8_t>(packed & 0xffu)),
        decode(static_cast<uint8_t>((packed >> 8u) & 0xffu)),
        decode(static_cast<uint8_t>((packed >> 16u) & 0xffu)),
    };
}

glm::vec3 authored_light_color(uint32_t packed, const glm::vec3& fallback)
{
    if ((packed & 0x00ffffffu) == 0u) {
        return fallback;
    }

    const auto decoded = srgb8_to_linear(packed);
    if (std::max(decoded.x, std::max(decoded.y, decoded.z)) <= 1.0e-4f) {
        return fallback;
    }
    return decoded;
}

float saturate(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float smooth_blend(float edge0, float edge1, float value)
{
    if (edge0 == edge1) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 mix_vec3(const glm::vec3& a, const glm::vec3& b, float t)
{
    return a + (b - a) * t;
}

glm::vec3 celestial_direction(float azimuth, float height)
{
    const glm::vec3 dir{
        std::cos(azimuth),
        std::sin(azimuth),
        std::max(height, 0.08f),
    };
    return glm::normalize(dir);
}

float viewer_area_cycle_time(const AppState& state, const PreviewScene& scene)
{
    if (!scene.is_area || scene.area_weather.day_night_cycle == 0) {
        return scene.area_weather.is_night ? 0.75f : 0.25f;
    }
    return std::fmod(std::max(state.area_day_night_elapsed, 0.0f), kAreaViewerDayNightCycleSeconds)
        / kAreaViewerDayNightCycleSeconds;
}

float twilight_weighted_cycle_time(float cycle_t)
{
    return cycle_t - (kAreaViewerTwilightHoldStrength / (4.0f * glm::pi<float>()))
        * std::sin(cycle_t * 4.0f * glm::pi<float>());
}

float initial_area_cycle_elapsed(const PreviewScene& scene)
{
    return scene.area_weather.is_night
        ? kAreaViewerDayNightCycleSeconds * 0.75f
        : kAreaViewerDayNightCycleSeconds * 0.25f;
}

void sync_area_day_night_state(AppState& state, bool log_transition)
{
    if (!state.current_scene || !should_tick_area_day_night(*state.current_scene)) {
        return;
    }

    const float cycle_t = twilight_weighted_cycle_time(viewer_area_cycle_time(state, *state.current_scene));
    const bool is_night = std::sin(cycle_t * glm::two_pi<float>()) < 0.0f;
    if (state.current_scene->area_weather.is_night != static_cast<uint8_t>(is_night)) {
        state.current_scene->area_weather.is_night = static_cast<uint8_t>(is_night);
        if (log_transition) {
            LOG_F(INFO, "Area viewer lighting crossed into {}", is_night ? "night" : "day");
        }
    }
}

void set_area_day_night_elapsed(AppState& state, float elapsed_seconds, bool log_transition)
{
    if (!state.current_scene || !should_tick_area_day_night(*state.current_scene)) {
        state.area_day_night_elapsed = 0.0f;
        return;
    }

    state.area_day_night_elapsed = std::fmod(elapsed_seconds, kAreaViewerDayNightCycleSeconds);
    if (state.area_day_night_elapsed < 0.0f) {
        state.area_day_night_elapsed += kAreaViewerDayNightCycleSeconds;
    }
    sync_area_day_night_state(state, log_transition);
}

bool supports_area_day_night_controls_impl(const AppState& state)
{
    return state.current_scene && should_tick_area_day_night(*state.current_scene);
}

bool supports_gltf_animation_controls_impl(const AppState& state)
{
    if (!state.current_scene || state.current_scene->is_area) return false;
    for (const auto& model : state.current_scene->static_models) {
        if (model && !model->animations.empty()) return true;
    }
    return false;
}

bool supports_vfx_sequence_controls_impl(const AppState& state)
{
    return state.current_scene && !state.vfx_sequence_steps.empty() && state.vfx_sequence_loop_ms > 0;
}

std::vector<std::string> collect_model_animation_names(const ModelInstance& model)
{
    std::vector<std::string> result;
    if (!model.scene_animation_enabled) {
        return result;
    }

    const nw::model::Mdl* source = model.animation_source_ ? model.animation_source_ : model.mdl_;
    while (source) {
        for (const auto& animation : source->model.animations) {
            if (!animation) {
                continue;
            }

            const std::string name = animation->name.c_str();
            if (!name.empty() && std::find(result.begin(), result.end(), name) == result.end()) {
                result.push_back(name);
            }
        }
        source = source->model.supermodel.get();
    }

    return result;
}

std::vector<std::string> collect_gltf_animation_names(const nw::render::RenderModel& model)
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

void rebuild_scene_animation_lists(AppState& state)
{
    state.model_animation_names.clear();
    state.gltf_animation_names.clear();
    if (!state.current_scene) {
        return;
    }

    state.model_animation_names.resize(state.current_scene->models.size());
    for (size_t i = 0; i < state.current_scene->models.size(); ++i) {
        const auto& model = state.current_scene->models[i];
        if (!model) {
            continue;
        }
        state.model_animation_names[i] = collect_model_animation_names(*model);
    }

    state.gltf_animation_names.resize(state.current_scene->static_models.size());
    for (size_t i = 0; i < state.current_scene->static_models.size(); ++i) {
        const auto& model = state.current_scene->static_models[i];
        if (!model) {
            continue;
        }
        state.gltf_animation_names[i] = collect_gltf_animation_names(*model);
    }
}

void rebuild_gltf_animation_backends(AppState& state)
{
    state.gltf_animation_backends.clear();
    state.gltf_poses.clear();
    if (!state.current_scene) return;

    state.gltf_animation_backends.resize(state.current_scene->static_models.size());
    state.gltf_poses.resize(state.current_scene->static_models.size());
    for (size_t i = 0; i < state.current_scene->static_models.size(); ++i) {
        const auto& model = state.current_scene->static_models[i];
        if (!model || model->animations.empty() || model->skeletons.empty()) {
            continue;
        }

        auto build_backend = [&](nw::render::AnimationBackendKind kind) {
            auto backend = nw::render::make_animation_backend(kind);
            if (!backend) return std::unique_ptr<nw::render::AnimationBackend>{};
            for (uint32_t s = 0; s < model->skeletons.size(); ++s) {
                if (!backend->build_skeleton(s, model->skeletons[s])) {
                    return std::unique_ptr<nw::render::AnimationBackend>{};
                }
            }
            for (uint32_t c = 0; c < model->animations.size(); ++c) {
                if (!backend->build_clip(c, model->animations[c])) {
                    return std::unique_ptr<nw::render::AnimationBackend>{};
                }
            }
            return backend;
        };

        auto backend = build_backend(nw::render::AnimationBackendKind::ozz);
        bool using_ozz = static_cast<bool>(backend);
        if (!backend) {
            backend = build_backend(nw::render::AnimationBackendKind::custom);
        }
        if (backend) {
            LOG_F(INFO, "glTF animation backend for model {}: {}", model->name, using_ozz ? "ozz" : "custom");
        } else {
            LOG_F(WARNING, "Failed to build any animation backend for model {}", model->name);
        }
        state.gltf_animation_backends[i] = std::move(backend);
    }
}

void rebuild_scene_animation_state(AppState& state)
{
    rebuild_scene_animation_lists(state);
    state.gltf_animation_clip = 0;
    state.gltf_animation_time = 0.0f;
    rebuild_gltf_animation_backends(state);
}

int32_t compute_particle_prime_ms(const PreviewScene& scene, bool explicit_animation)
{
    auto first_effect_event_ms = [](const auto& events) -> std::optional<int32_t> {
        if (events.empty()) {
            return std::nullopt;
        }
        return std::max(0, static_cast<int32_t>(std::ceil(events.front().time * 1000.0f)));
    };

    auto first_positive_track_key = [](const auto& keys) -> const nw::render::ParticleCurveKeyF32* {
        for (const auto& key : keys) {
            if (key.value > 0.0f) {
                return &key;
            }
        }
        return nullptr;
    };

    if (explicit_animation) {
        int32_t prime_ms = 16;
        for (const auto& scene_particles : scene.particles) {
            const auto effect_event_ms = first_effect_event_ms(scene_particles.import.effect_events);
            const auto count = std::min(scene_particles.compiled.effect.emitters.size(), scene_particles.import.effect.emitters.size());
            for (size_t i = 0; i < count; ++i) {
                const auto& emitter = scene_particles.compiled.effect.emitters[i];
                const auto& authored = scene_particles.import.effect.emitters[i];
                int32_t activation_ms = 0;

                if (emitter.emission.mode == nw::render::ParticleEmissionMode::event_burst
                    || emitter.emission.trigger_on_effect_events) {
                    if (effect_event_ms) {
                        activation_ms = *effect_event_ms;
                    }
                } else if (const auto* active_key = first_positive_track_key(emitter.emission_rate_track.keys)) {
                    activation_ms = std::max(0, static_cast<int32_t>(std::ceil(active_key->time * 1000.0f)));
                }

                float visible_rate = emitter.emission.rate;
                if (const auto* active_key = first_positive_track_key(emitter.emission_rate_track.keys)) {
                    visible_rate = active_key->value;
                }

                int32_t settle_ms = 33;
                if (emitter.emission.mode == nw::render::ParticleEmissionMode::continuous
                    && emitter.emission.metric == nw::render::ParticleSpawnMetric::per_second
                    && visible_rate > 0.0f) {
                    const int32_t interval_ms = static_cast<int32_t>(std::ceil(1000.0f / visible_rate));
                    const float lifetime = std::max(authored.initial.lifetime.min, authored.initial.lifetime.max);
                    const int32_t lifetime_ms = static_cast<int32_t>(std::ceil(std::max(0.0f, lifetime) * 1000.0f * 0.25f));
                    settle_ms = std::max({150, interval_ms + 16, lifetime_ms});
                }

                prime_ms = std::max(prime_ms, activation_ms + settle_ms);
            }
        }
        return std::min(prime_ms, 3000);
    }

    int32_t prime_ms = 250;
    bool has_fade_in_continuous = false;
    for (const auto& scene_particles : scene.particles) {
        const auto count = std::min(scene_particles.compiled.effect.emitters.size(), scene_particles.import.effect.emitters.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& emitter = scene_particles.compiled.effect.emitters[i];
            const auto& authored = scene_particles.import.effect.emitters[i];
            if (emitter.emission.mode != nw::render::ParticleEmissionMode::continuous) {
                continue;
            }
            if (emitter.emission.metric != nw::render::ParticleSpawnMetric::per_second) {
                continue;
            }

            float rate = emitter.emission.rate;
            if (!emitter.emission_rate_track.keys.empty()) {
                rate = emitter.emission_rate_track.keys.front().value;
            }
            if (rate <= 0.0f) {
                continue;
            }

            const int32_t interval_ms = static_cast<int32_t>(std::ceil(1000.0f / rate));
            const float lifetime = std::max(authored.initial.lifetime.min, authored.initial.lifetime.max);
            bool fades_in_over_life = false;
            if (!authored.over_life.alpha.keys.empty()) {
                const float alpha_start = authored.over_life.alpha.keys.front().value;
                const float alpha_end = authored.over_life.alpha.keys.back().value;
                fades_in_over_life = alpha_end > alpha_start + 1.0e-4f;
            }
            has_fade_in_continuous = has_fade_in_continuous || fades_in_over_life;
            const float settle_lifetime = fades_in_over_life ? lifetime * 0.75f : lifetime * 0.5f;
            const int32_t visible_settle_ms = static_cast<int32_t>(std::ceil(std::max(0.0f, settle_lifetime) * 1000.0f));
            prime_ms = std::max(prime_ms, std::max(interval_ms + 16, visible_settle_ms));
        }
    }

    return std::min(prime_ms, has_fade_in_continuous ? 3000 : 1500);
}

struct AreaCycleWeights {
    float cycle_t = 0.25f;
    float orbit_angle = glm::half_pi<float>();
    float sun_height = 1.0f;
    float sun_weight = 1.0f;
    float moon_weight = 0.0f;
    float azimuth = 0.0f;
    float dusk_strength = 0.0f;
};

AreaCycleWeights resolve_area_cycle_weights(const AppState& state, const PreviewScene& scene)
{
    AreaCycleWeights result{};
    result.cycle_t = twilight_weighted_cycle_time(viewer_area_cycle_time(state, scene));
    result.orbit_angle = result.cycle_t * glm::two_pi<float>();
    result.sun_height = std::sin(result.orbit_angle);
    result.sun_weight = smooth_blend(-0.38f, 0.34f, result.sun_height);
    result.moon_weight = 1.0f - result.sun_weight;
    result.azimuth = result.orbit_angle - glm::half_pi<float>();
    const float twilight = 1.0f - std::abs(result.sun_weight * 2.0f - 1.0f);
    result.dusk_strength = twilight * twilight;
    return result;
}

Lighting gltf_preview_lighting()
{
    Lighting result{};
    result.key_intensity = 0.0f;
    result.fill_intensity = 0.0f;
    result.rim_intensity = 0.0f;
    result.ambient = glm::vec3{0.0f};
    return result;
}

Lighting resolve_scene_lighting(const AppState& state, const PreviewScene& scene)
{
    if (!scene.static_models.empty()) {
        return gltf_preview_lighting();
    }

    if (!scene.is_area || (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none) {
        return {};
    }

    const auto cycle = resolve_area_cycle_weights(state, scene);
    const glm::vec3 dusk_warmth{1.0f, 0.52f, 0.24f};
    const glm::vec3 dusk_ambient{0.34f, 0.18f, 0.10f};

    const glm::vec3 sun_fill_dir = celestial_direction(cycle.azimuth + 0.9f, std::max(cycle.sun_height * 0.65f, 0.1f));
    const glm::vec3 moon_fill_dir = celestial_direction(cycle.azimuth + glm::pi<float>() - 0.75f, std::max(-cycle.sun_height * 0.55f, 0.08f));
    const float key_azimuth = cycle.azimuth + cycle.moon_weight * glm::pi<float>();
    const float key_height = cycle.moon_weight * std::max(-cycle.sun_height, 0.1f) + cycle.sun_weight * std::max(cycle.sun_height, 0.12f);

    Lighting result{};
    result.key_direction = celestial_direction(key_azimuth, key_height);
    result.key_color = mix_vec3(
        authored_light_color(scene.area_weather.color_moon_diffuse, glm::vec3{0.48f, 0.56f, 0.72f}),
        authored_light_color(scene.area_weather.color_sun_diffuse, glm::vec3{1.0f, 0.96f, 0.86f}),
        cycle.sun_weight);
    result.key_color = mix_vec3(result.key_color, dusk_warmth, cycle.dusk_strength * 0.55f);
    result.key_intensity = cycle.moon_weight * 0.52f + cycle.sun_weight * 2.15f + cycle.dusk_strength * 0.18f;
    result.fill_direction = glm::normalize(mix_vec3(moon_fill_dir, sun_fill_dir, cycle.sun_weight));
    result.fill_color = mix_vec3(glm::vec3{0.16f, 0.22f, 0.32f}, glm::vec3{0.58f, 0.66f, 0.78f}, cycle.sun_weight);
    result.fill_color = mix_vec3(result.fill_color, dusk_warmth, cycle.dusk_strength * 0.22f);
    result.fill_intensity = cycle.moon_weight * 0.11f + cycle.sun_weight * 0.42f + cycle.dusk_strength * 0.06f;
    result.rim_direction = celestial_direction(cycle.azimuth + glm::pi<float>(), 0.16f + 0.08f * cycle.sun_weight);
    result.rim_color = dusk_warmth;
    result.rim_intensity = cycle.dusk_strength * (0.08f + 0.08f * cycle.sun_weight);
    result.ambient = mix_vec3(
        authored_light_color(scene.area_weather.color_moon_ambient, glm::vec3{0.035f, 0.04f, 0.055f}),
        authored_light_color(scene.area_weather.color_sun_ambient, glm::vec3{0.25f, 0.26f, 0.27f}),
        cycle.sun_weight);
    result.ambient = mix_vec3(result.ambient, dusk_ambient, cycle.dusk_strength * 0.35f);

    return result;
}

SceneFog resolve_scene_fog(const AppState& state, const PreviewScene& scene)
{
    if (!state.show_authored_area_fog) {
        return {};
    }

    if (!scene.is_area || (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none) {
        return {};
    }

    const auto& weather = scene.area_weather;
    const bool has_sun_color = (weather.color_sun_fog & 0x00ffffffu) != 0u;
    const bool has_moon_color = (weather.color_moon_fog & 0x00ffffffu) != 0u;
    if ((!has_sun_color && !has_moon_color) || weather.fog_clip_distance <= 0.0f) {
        return {};
    }

    const auto cycle = resolve_area_cycle_weights(state, scene);
    const glm::vec3 sun_color = has_sun_color
        ? srgb8_to_linear(weather.color_sun_fog)
        : srgb8_to_linear(weather.color_moon_fog);
    const glm::vec3 moon_color = has_moon_color
        ? srgb8_to_linear(weather.color_moon_fog)
        : srgb8_to_linear(weather.color_sun_fog);

    const float moon_amount = std::clamp(static_cast<float>(weather.fog_moon_amount) / 15.0f, 0.0f, 1.0f);
    const float sun_amount = std::clamp(static_cast<float>(weather.fog_sun_amount) / 15.0f, 0.0f, 1.0f);
    const float authored_amount = moon_amount + (sun_amount - moon_amount) * cycle.sun_weight;
    const float preview_amount = authored_amount > 0.0f ? authored_amount : 0.18f;

    SceneFog result{};
    result.enabled = true;
    result.color = mix_vec3(moon_color, sun_color, cycle.sun_weight);
    result.amount = preview_amount;
    result.end_distance = weather.fog_clip_distance;
    result.start_distance = result.end_distance * glm::mix(0.76f, 0.50f, preview_amount);
    return result;
}

LightingSpace resolve_scene_lighting_space(const PreviewScene& scene)
{
    if (scene.is_area && (scene.area_flags & nw::AreaFlags::interior) == nw::AreaFlags::none) {
        return LightingSpace::world_space;
    }
    return LightingSpace::camera_relative;
}

std::array<glm::vec3, 8> bounds_corners_world(const Bounds& bounds)
{
    std::array<glm::vec3, 8> corners{};
    size_t index = 0;
    for (float z : {bounds.min.z, bounds.max.z}) {
        for (float y : {bounds.min.y, bounds.max.y}) {
            for (float x : {bounds.min.x, bounds.max.x}) {
                corners[index++] = glm::vec3{x, y, z};
            }
        }
    }
    return corners;
}

std::array<glm::vec3, 8> frustum_corners_world(const glm::mat4& inv_view_projection)
{
    std::array<glm::vec3, 8> corners{};
    size_t index = 0;
    for (float z : {0.0f, 1.0f}) {
        for (float y : {-1.0f, 1.0f}) {
            for (float x : {-1.0f, 1.0f}) {
                glm::vec4 corner = inv_view_projection * glm::vec4{x, y, z, 1.0f};
                corners[index++] = glm::vec3{corner} / corner.w;
            }
        }
    }
    return corners;
}

std::array<glm::vec3, 8> slice_frustum_corners(const std::array<glm::vec3, 8>& frustum_corners, float near_ratio, float far_ratio)
{
    std::array<glm::vec3, 8> slice{};
    for (size_t i = 0; i < 4; ++i) {
        const glm::vec3 near_corner = frustum_corners[i];
        const glm::vec3 far_corner = frustum_corners[i + 4];
        slice[i] = near_corner + (far_corner - near_corner) * near_ratio;
        slice[i + 4] = near_corner + (far_corner - near_corner) * far_ratio;
    }
    return slice;
}

glm::mat4 fit_shadow_matrix(const std::array<glm::vec3, 8>& corners, const glm::vec3& light_dir, float radius_scale)
{
    glm::vec3 center{0.0f};
    for (const auto& corner : corners) {
        center += corner;
    }
    center /= static_cast<float>(corners.size());

    const glm::vec3 eye = center + light_dir * radius_scale;
    glm::mat4 light_view = glm::lookAtRH(eye, center, glm::vec3{0.0f, 0.0f, 1.0f});

    glm::vec3 mins{std::numeric_limits<float>::max()};
    glm::vec3 maxs{std::numeric_limits<float>::lowest()};
    for (const auto& corner : corners) {
        const glm::vec3 light_space = glm::vec3(light_view * glm::vec4{corner, 1.0f});
        mins = glm::min(mins, light_space);
        maxs = glm::max(maxs, light_space);
    }

    const float padding = 8.0f;
    mins.z -= padding;
    maxs.z += padding;

    const float extent_x = maxs.x - mins.x;
    const float extent_y = maxs.y - mins.y;
    const float texel_x = extent_x / static_cast<float>(kShadowMapResolution);
    const float texel_y = extent_y / static_cast<float>(kShadowMapResolution);
    if (texel_x > 0.0f) {
        mins.x = std::floor(mins.x / texel_x) * texel_x;
        maxs.x = std::floor(maxs.x / texel_x) * texel_x;
    }
    if (texel_y > 0.0f) {
        mins.y = std::floor(mins.y / texel_y) * texel_y;
        maxs.y = std::floor(maxs.y / texel_y) * texel_y;
    }

    return glm::orthoRH_ZO(mins.x, maxs.x, mins.y, maxs.y, -maxs.z, -mins.z) * light_view;
}

SceneShadow resolve_scene_shadow(const RenderContext& ctx, const Bounds& bounds)
{
    if (ctx.lighting_space != LightingSpace::world_space) {
        return {};
    }

    const glm::vec3 light_dir = glm::normalize(ctx.lighting.key_direction);
    if (!std::isfinite(light_dir.x) || !std::isfinite(light_dir.y) || !std::isfinite(light_dir.z)
        || glm::dot(light_dir, light_dir) < 1.0e-6f) {
        return {};
    }

    SceneShadow result{};
    result.enabled = true;
    result.strength = 0.85f;

    const glm::mat4 inv_view_projection = glm::inverse(ctx.projection * ctx.view);
    const auto frustum_corners = frustum_corners_world(inv_view_projection);
    const float shadow_distance = std::min(ctx.camera_far_plane,
        ctx.orthographic_camera ? kShadowMaxDistance * 1.5f : kShadowMaxDistance);
    const float shadow_ratio = std::clamp(shadow_distance / std::max(ctx.camera_far_plane, 1.0e-4f), 0.0f, 1.0f);
    const std::array<float, kShadowCascadeCount> split_ratios = ctx.orthographic_camera
        ? std::array<float, kShadowCascadeCount>{0.22f, 0.55f, 1.0f}
        : std::array<float, kShadowCascadeCount>{0.10f, 0.32f, 1.0f};
    float prev_ratio = 0.0f;
    for (size_t i = 0; i < kShadowCascadeCount; ++i) {
        const float far_ratio = split_ratios[i] * shadow_ratio;
        result.split_distances[i] = shadow_distance * split_ratios[i];
        auto corners = slice_frustum_corners(frustum_corners, prev_ratio, far_ratio);
        result.world_to_shadow[i] = fit_shadow_matrix(corners, light_dir, std::max(bounds.radius() * 2.5f, 80.0f));
        prev_ratio = far_ratio;
    }

    result.world_to_shadow[kShadowCascadeCount - 1] = fit_shadow_matrix(bounds_corners_world(bounds), light_dir,
        std::max(bounds.radius() * 2.5f, 80.0f));
    return result;
}

bool should_tick_area_day_night(const PreviewScene& scene)
{
    return scene.is_area
        && (scene.area_flags & nw::AreaFlags::interior) == nw::AreaFlags::none
        && scene.area_weather.day_night_cycle != 0;
}

void update_area_day_night(AppState& state, float dt)
{
    if (!supports_area_day_night_controls_impl(state)) {
        state.area_day_night_elapsed = 0.0f;
        return;
    }

    if (!state.area_day_night_autoplay) {
        sync_area_day_night_state(state, false);
        return;
    }

    set_area_day_night_elapsed(state, state.area_day_night_elapsed + std::max(dt, 0.0f), true);
}

std::pair<float, float> camera_clip_planes(const Camera& camera, const Bounds& bounds)
{
    if (!camera.is_orthographic()) {
        const auto center = bounds.center();
        const float radius = std::max(bounds.radius(), 1.0f);
        const float distance = glm::length(camera.get_position() - center);
        const float near_plane = std::max(0.25f, std::min(distance * 0.25f, std::max(0.25f, distance - radius * 1.5f)));
        const float far_plane = std::max(near_plane + 50.0f, distance + radius * 2.5f);
        return {near_plane, far_plane};
    }

    const glm::mat4 view = camera.get_view_matrix();
    const std::array<glm::vec3, 8> corners{{
        {bounds.min.x, bounds.min.y, bounds.min.z},
        {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.min.z},
        {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.min.z},
        {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.min.z},
        {bounds.max.x, bounds.max.y, bounds.max.z},
    }};

    float min_distance = std::numeric_limits<float>::max();
    float max_distance = 0.0f;
    for (const auto& corner : corners) {
        const glm::vec4 view_corner = view * glm::vec4(corner, 1.0f);
        const float distance = -view_corner.z;
        min_distance = std::min(min_distance, distance);
        max_distance = std::max(max_distance, distance);
    }

    const float padding = std::max(bounds.radius() * 0.1f, 25.0f);
    const float near_plane = std::max(0.1f, min_distance - padding);
    const float far_plane = std::max(near_plane + 100.0f, max_distance + padding);
    return {near_plane, far_plane};
}

float keyboard_speed_scale(const SDL_KeyboardEvent& key)
{
    return (key.mod & SDL_KMOD_SHIFT) ? 3.0f : 1.0f;
}

std::string current_capture_path(const AppState& state)
{
    if (state.turntable_capture) {
        auto dir = state.output_dir.empty() ? std::filesystem::path{"."} : std::filesystem::path{state.output_dir};
        char file_name[32];
        std::snprintf(file_name, sizeof(file_name), "%04d.png", state.capture_index);
        return (dir / file_name).string();
    }
    return state.screenshot_path;
}

void clear_vfx_sequence(AppState& state)
{
    vfx_sequence_clear_state(state);
}

void reset_vfx_sequence(AppState& state)
{
    vfx_sequence_reset_runtime(state);
}

void update_vfx_sequence(AppState& state, int32_t dt_ms)
{
    vfx_sequence_update_runtime(state, dt_ms);
}

void advance_scene_playback(AppState& state, int32_t dt_ms, bool force_step)
{
    if (!state.current_scene) {
        return;
    }

    dt_ms = std::max(dt_ms, 0);
    const float dt = static_cast<float>(dt_ms) * 0.001f;
    const bool supports_vfx = supports_vfx_sequence_controls_impl(state);
    const bool tick_vfx = force_step || !supports_vfx || state.vfx_sequence_autoplay;
    const int32_t scene_dt_ms = force_step ? dt_ms : (tick_vfx ? dt_ms : 0);

    if (tick_vfx && supports_vfx) {
        update_vfx_sequence(state, dt_ms);
    }

    state.current_scene->update(scene_dt_ms);

    if (supports_gltf_animation_controls_impl(state) && (force_step || state.gltf_animation_autoplay)) {
        state.gltf_animation_time += dt;
    }

    if (force_step || state.area_day_night_autoplay) {
        update_area_day_night(state, dt);
    }
}

void fit_area_navigation_camera(Camera& camera, const Bounds& bounds)
{
    camera.fit_to_bounds(bounds);
    camera.set_free_view(camera.get_position(), camera.get_target(), Camera::ProjectionMode::perspective);
}

void apply_gltf_preview_tuning(AppState& state, const PreviewScene& scene)
{
    if (!scene.static_models.empty()) {
        state.gltf_ibl_strength = 0.80f;
        state.gltf_exposure = 1.00f;
    } else {
        state.gltf_ibl_strength = 1.0f;
        state.gltf_exposure = 1.0f;
    }
}

} // namespace

bool scene_uses_shared_nwn_animation_source(const PreviewScene& scene)
{
    const nw::model::Mdl* shared_source = nullptr;
    bool saw_animatable = false;
    for (const auto& model : scene.models) {
        if (!model || !model->scene_animation_enabled) {
            continue;
        }

        const auto* source = model->animation_source_ ? model->animation_source_ : model->mdl_;
        if (!source) {
            continue;
        }

        if (!shared_source) {
            shared_source = source;
        } else if (shared_source != source) {
            return false;
        }
        saw_animatable = true;
    }
    return saw_animatable;
}

bool has_area_day_night_controls(const AppState& state)
{
    return supports_area_day_night_controls_impl(state);
}

bool supports_gltf_animation_controls(const AppState& state)
{
    return supports_gltf_animation_controls_impl(state);
}

bool supports_vfx_sequence_controls(const AppState& state)
{
    return supports_vfx_sequence_controls_impl(state);
}

void set_area_day_night_elapsed_seconds(AppState& state, float elapsed_seconds, bool log_transition)
{
    set_area_day_night_elapsed(state, elapsed_seconds, log_transition);
}

void reset_area_day_night_cycle(AppState& state)
{
    if (!state.current_scene || !supports_area_day_night_controls_impl(state)) {
        return;
    }
    set_area_day_night_elapsed(state, initial_area_cycle_elapsed(*state.current_scene), false);
}

void load_model(AppState& state, std::string_view resref)
{
    LOG_F(INFO, "Loading model: {}", resref);
    clear_vfx_sequence(state);
    state.loaded_scene_kind = LoadedSceneKind::model;
    state.loaded_scene_source = std::string{resref};
    state.loaded_vfx_sequence.reset();

    state.current_scene = load_preview_scene(*state.renderer, resref);
    rebuild_scene_animation_state(state);

    if (state.current_scene) {
        if (!state.current_scene->static_models.empty()) {
            ensure_gltf_ibl_textures(state);
        }
        apply_gltf_preview_tuning(state, *state.current_scene);

        LOG_F(INFO, "Model loaded: {} vertices, {} indices",
            state.current_scene->vertex_count,
            state.current_scene->index_count);
        const auto bounds = state.current_scene->current_bounds();
        LOG_F(INFO, "Model bounds: min=({}, {}, {}) max=({}, {}, {}) center=({}, {}, {}) radius={}",
            bounds.min.x, bounds.min.y, bounds.min.z,
            bounds.max.x, bounds.max.y, bounds.max.z,
            bounds.center().x, bounds.center().y, bounds.center().z,
            bounds.radius());

        bool has_idle = false;
        if (!state.current_scene->models.empty()) {
            auto try_scene_animation = [&](std::string_view animation) {
                if (!has_idle && !animation.empty()) {
                    has_idle = state.current_scene->load_animation(animation);
                }
            };
            try_scene_animation(state.animation_override);
            if (const auto* mdl = state.current_scene->models.front()->mdl_) {
                try_scene_animation(preferred_model_animation_name(*mdl, PreferredModelAnimationContext::hold));
            }
            try_scene_animation("default");
            try_scene_animation("on");
            try_scene_animation("cast01");
            try_scene_animation("impact");
            try_scene_animation("pause1");
            try_scene_animation("cpause1");
        }

        if (has_idle) {
            state.current_scene->update(33);
        }
        if (!state.current_scene->particles.empty()) {
            const int32_t particle_prime_ms = compute_particle_prime_ms(*state.current_scene, !state.animation_override.empty());
            state.current_scene->update(particle_prime_ms);
            size_t live_particles = 0;
            for (const auto& scene_particles : state.current_scene->particles) {
                live_particles += scene_particles.system.particles.core.position.size();
            }
            LOG_F(INFO, "Primed {} particle systems ({} live particles, {} ms)",
                state.current_scene->particles.size(), live_particles, particle_prime_ms);
        }

        if (state.camera) {
            state.camera->set_fov(60.0f);
            const auto camera_bounds = state.current_scene->current_bounds();
            if (!state.current_scene->static_models.empty()) {
                fit_static_model_camera(*state.camera, camera_bounds);
            } else {
                state.camera->fit_to_bounds(camera_bounds);
            }
        }
    } else {
        LOG_F(ERROR, "Failed to load model: {}", resref);
    }

    state.area_navigation = false;
    state.area_day_night_autoplay = true;
    state.area_day_night_elapsed = 0.0f;
    state.gltf_animation_autoplay = true;
    state.vfx_sequence_autoplay = true;
    state.scene_playing = true;
}

void load_vfx_sequence(AppState& state, const VfxSequence& sequence)
{
    LOG_F(INFO, "Loading VFX sequence: {}", sequence.label);
    clear_vfx_sequence(state);
    state.loaded_scene_kind = LoadedSceneKind::vfx_sequence;
    state.loaded_scene_source.clear();
    state.loaded_vfx_sequence = sequence;

    std::vector<std::string> sources;
    const bool use_source_target_layout = sequence.source_target_layout;
    const bool use_spell_actors = sequence.use_spell_actors;
    sources.reserve(sequence.steps.size() + (use_spell_actors ? 2u : 0u));
    if (use_spell_actors) {
        sources.emplace_back(kVfxSequenceCasterModel);
        sources.emplace_back(kVfxSequenceTargetModel);
    }
    for (const auto& step : sequence.steps) {
        sources.push_back(step.source);
    }

    state.current_scene = load_preview_scene(*state.renderer, sources);
    rebuild_scene_animation_state(state);
    if (!state.current_scene) {
        LOG_F(ERROR, "Failed to load VFX sequence: {}", sequence.label);
        return;
    }

    const auto layout = vfx_sequence_prepare_scene(
        state, *state.current_scene, sequence, state.animation_override);

    bool has_idle = false;
    auto try_sequence_animation = [&](std::string_view animation) {
        if (animation.empty()) {
            return;
        }
        has_idle = state.current_scene->load_animation(animation) || has_idle;
    };
    try_sequence_animation(state.animation_override);
    if (!use_source_target_layout) {
        try_sequence_animation("default");
        try_sequence_animation("on");
        try_sequence_animation("cast01");
        try_sequence_animation("pause1");
    }

    if (state.camera) {
        if (use_source_target_layout) {
            const glm::vec3 camera_target_pos = vfx_sequence_resolve_target_point(
                layout.target, layout.target_root_pos, VfxTargetPointKind::center);
            const glm::vec3 center = 0.5f * (layout.source_pos + camera_target_pos) + glm::vec3{0.0f, 0.0f, 1.2f};
            state.camera->set_fov(45.0f);
            state.camera->set_orbit_view(
                center, layout.distance * 1.18f, -90.0f, 20.0f, Camera::ProjectionMode::perspective);
        } else {
            state.camera->set_fov(60.0f);
            state.camera->fit_to_bounds(state.current_scene->current_bounds());
        }
    }

    reset_vfx_sequence(state);
    update_vfx_sequence(state, 0);
    LOG_F(INFO, "Loaded VFX sequence '{}' with {} steps over {} ms",
        sequence.label, state.vfx_sequence_steps.size(), state.vfx_sequence_loop_ms);
    state.area_navigation = false;
    state.area_day_night_autoplay = true;
    state.area_day_night_elapsed = 0.0f;
    state.gltf_animation_autoplay = true;
    state.vfx_sequence_autoplay = true;
    state.scene_playing = true;
}

void load_area(AppState& state, std::string_view resref)
{
    LOG_F(INFO, "Loading area: {}", resref);
    state.loaded_scene_kind = LoadedSceneKind::area;
    state.loaded_scene_source = std::string{resref};
    state.loaded_vfx_sequence.reset();

    state.current_scene = load_area_scene(*state.renderer, resref);
    rebuild_scene_animation_state(state);
    if (state.current_scene) {
        LOG_F(INFO, "Area loaded: {} vertices, {} indices",
            state.current_scene->vertex_count,
            state.current_scene->index_count);
        const auto area_bounds = state.current_scene->current_bounds();
        LOG_F(INFO, "Area bounds: min=({}, {}, {}) max=({}, {}, {})",
            area_bounds.min.x, area_bounds.min.y, area_bounds.min.z,
            area_bounds.max.x, area_bounds.max.y, area_bounds.max.z);

        if (state.camera) {
            state.camera->set_fov(45.0f);
            fit_area_navigation_camera(*state.camera, area_bounds);
        }
        state.area_navigation = true;
        state.area_day_night_autoplay = true;
        state.gltf_animation_autoplay = true;
        state.vfx_sequence_autoplay = true;
        state.scene_playing = true;
        set_area_day_night_elapsed(state, initial_area_cycle_elapsed(*state.current_scene), false);
    } else {
        LOG_F(ERROR, "Failed to load area: {}", resref);
        state.area_navigation = false;
        state.area_day_night_autoplay = true;
        state.area_day_night_elapsed = 0.0f;
        state.gltf_animation_autoplay = true;
        state.vfx_sequence_autoplay = true;
        state.scene_playing = true;
    }
}

void reload_current_scene(AppState& state)
{
    switch (state.loaded_scene_kind) {
    case LoadedSceneKind::model:
        if (!state.loaded_scene_source.empty()) {
            load_model(state, state.loaded_scene_source);
        }
        break;
    case LoadedSceneKind::vfx_sequence:
        if (state.loaded_vfx_sequence) {
            load_vfx_sequence(state, *state.loaded_vfx_sequence);
        }
        break;
    case LoadedSceneKind::area:
        if (!state.loaded_scene_source.empty()) {
            load_area(state, state.loaded_scene_source);
        }
        break;
    case LoadedSceneKind::none:
        break;
    }
}

bool select_model_animation(AppState& state, size_t model_index, std::string_view animation_name)
{
    if (!state.current_scene || animation_name.empty() || model_index >= state.current_scene->models.size()) {
        return false;
    }

    auto& model = state.current_scene->models[model_index];
    if (!model || !model->scene_animation_enabled) {
        return false;
    }

    bool loaded = false;
    for (auto& candidate : state.current_scene->models) {
        if (!candidate || !candidate->scene_animation_enabled) {
            continue;
        }

        candidate->anim_cursor_ = 0;
        loaded = candidate->load_animation(animation_name) || loaded;
    }

    if (!loaded) {
        return false;
    }

    state.current_scene->update(0);
    if (!state.current_scene->particles.empty()) {
        state.current_scene->rebuild_particles();
        const int32_t particle_prime_ms = compute_particle_prime_ms(*state.current_scene, true);
        state.current_scene->update(particle_prime_ms);

        size_t live_particles = 0;
        for (const auto& scene_particles : state.current_scene->particles) {
            live_particles += scene_particles.system.particles.core.position.size();
        }
        LOG_F(INFO, "Primed {} particle systems after switching to {} ({} live particles, {} ms)",
            state.current_scene->particles.size(), animation_name, live_particles, particle_prime_ms);
    }

    return true;
}

void set_gltf_animation_clip(AppState& state, uint32_t clip_index)
{
    if (!state.current_scene) {
        return;
    }

    state.gltf_animation_clip = clip_index;
    state.gltf_animation_time = 0.0f;

    for (const auto& model : state.current_scene->static_models) {
        if (!model || model->animations.empty()) {
            continue;
        }

        const auto& clip = model->animations[clip_index % model->animations.size()];
        LOG_F(INFO, "glTF animation clip {} on {}: {}",
            clip_index, model->name, clip.name.empty() ? "<unnamed>" : clip.name);
        break;
    }
}

void update_viewer(AppState& state, float dt)
{
    if (state.camera) {
        state.camera->update(dt);
    }

    if (state.current_scene && state.scene_playing) {
        advance_scene_playback(state, static_cast<int32_t>(dt * 1000.f), false);
    }

    if (state.turntable_mode && state.current_scene) {
        float angle = static_cast<float>(state.capture_index) / static_cast<float>(state.turntable_frames) * 360.0f;
        state.camera->set_orbit_angle(angle);
    }
}

void resume_scene_playback(AppState& state)
{
    state.scene_playing = true;
    if (supports_vfx_sequence_controls_impl(state) && !state.vfx_sequence_autoplay) {
        state.vfx_sequence_autoplay = true;
    }
    if (supports_gltf_animation_controls_impl(state) && !state.gltf_animation_autoplay) {
        state.gltf_animation_autoplay = true;
    }
    if (supports_area_day_night_controls_impl(state) && !state.area_day_night_autoplay) {
        state.area_day_night_autoplay = true;
    }
    LOG_F(INFO, "Scene playing");
}

void toggle_scene_playback(AppState& state)
{
    if (state.scene_playing) {
        state.scene_playing = false;
        LOG_F(INFO, "Scene paused");
    } else {
        resume_scene_playback(state);
    }
}

void stop_scene_playback(AppState& state, bool log_action)
{
    state.scene_playing = false;
    if (supports_vfx_sequence_controls_impl(state)) {
        state.vfx_sequence_autoplay = false;
    }
    if (supports_gltf_animation_controls_impl(state)) {
        state.gltf_animation_autoplay = false;
    }
    if (supports_area_day_night_controls_impl(state)) {
        state.area_day_night_autoplay = false;
    }
    if (log_action) {
        LOG_F(INFO, "Scene stopped");
    }
}

void reset_scene_playback(AppState& state)
{
    if (!state.current_scene) {
        return;
    }

    if (supports_vfx_sequence_controls_impl(state)) {
        reset_vfx_sequence(state);
        update_vfx_sequence(state, 0);
    }
    if (supports_gltf_animation_controls_impl(state)) {
        state.gltf_animation_time = 0.0f;
    }
    if (supports_area_day_night_controls_impl(state)) {
        reset_area_day_night_cycle(state);
    }

    stop_scene_playback(state, false);
    LOG_F(INFO, "Scene reset");
}

void step_scene_playback(AppState& state, int32_t dt_ms)
{
    if (!state.current_scene) {
        return;
    }

    advance_scene_playback(state, dt_ms, true);
    stop_scene_playback(state, false);
    LOG_F(INFO, "Scene stepped by {} ms", std::max(dt_ms, 0));
}

void handle_key_down(AppState& state, const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_ESCAPE) {
        state.running = false;
        return;
    }

    if (!state.camera) {
        return;
    }

    float scale = keyboard_speed_scale(key);
    const float move_speed = 2.5f * scale;
    const float rotate_speed = 6.0f * scale;
    switch (key.key) {
    case SDLK_F5:
        request_renderer_reload(state);
        break;
    case SDLK_P:
        toggle_scene_playback(state);
        break;
    case SDLK_SPACE:
        if (supports_vfx_sequence_controls_impl(state)) {
            state.vfx_sequence_autoplay = !state.vfx_sequence_autoplay;
            if (state.vfx_sequence_autoplay) {
                state.scene_playing = true;
            }
            LOG_F(INFO, "VFX sequence autoplay {}", state.vfx_sequence_autoplay ? "enabled" : "disabled");
        } else if (supports_gltf_animation_controls_impl(state)) {
            state.gltf_animation_autoplay = !state.gltf_animation_autoplay;
            if (state.gltf_animation_autoplay) {
                state.scene_playing = true;
            }
            LOG_F(INFO, "glTF animation autoplay {}", state.gltf_animation_autoplay ? "enabled" : "disabled");
        } else if (supports_area_day_night_controls_impl(state)) {
            state.area_day_night_autoplay = !state.area_day_night_autoplay;
            if (state.area_day_night_autoplay) {
                state.scene_playing = true;
            }
            LOG_F(INFO, "Area day/night autoplay {}", state.area_day_night_autoplay ? "enabled" : "disabled");
        }
        break;
    case SDLK_PERIOD:
        if (state.current_scene) {
            step_scene_playback(state, kVfxSequenceStepMs);
        }
        break;
    case SDLK_SLASH:
        if (state.current_scene) {
            reset_scene_playback(state);
        }
        break;
    case SDLK_TAB:
        if (supports_gltf_animation_controls_impl(state)) {
            set_gltf_animation_clip(state, state.gltf_animation_clip + 1);
        }
        break;
    case SDLK_LEFTBRACKET:
        if (supports_gltf_animation_controls_impl(state)) {
            state.gltf_animation_time = std::max(0.0f, state.gltf_animation_time - 0.25f * scale);
            LOG_F(INFO, "glTF animation time {:.2f}", state.gltf_animation_time);
        }
        break;
    case SDLK_RIGHTBRACKET:
        if (supports_gltf_animation_controls_impl(state)) {
            state.gltf_animation_time += 0.25f * scale;
            LOG_F(INFO, "glTF animation time {:.2f}", state.gltf_animation_time);
        }
        break;
    case SDLK_G:
        state.show_authored_area_fog = !state.show_authored_area_fog;
        LOG_F(INFO, "Authored area fog {}", state.show_authored_area_fog ? "enabled" : "disabled");
        if (state.show_authored_area_fog && state.current_scene) {
            const auto fog = resolve_scene_fog(state, *state.current_scene);
            if (fog.enabled) {
                LOG_F(INFO,
                    "Resolved area fog: amount={:.3f} start={:.2f} end={:.2f} color=({:.3f}, {:.3f}, {:.3f}) authored_clip={:.2f} authored_sun={} authored_moon={}",
                    fog.amount,
                    fog.start_distance,
                    fog.end_distance,
                    fog.color.r,
                    fog.color.g,
                    fog.color.b,
                    state.current_scene->area_weather.fog_clip_distance,
                    state.current_scene->area_weather.fog_sun_amount,
                    state.current_scene->area_weather.fog_moon_amount);
            } else {
                LOG_F(INFO,
                    "Resolved area fog disabled: sun_color=0x{:06x} moon_color=0x{:06x} clip={:.2f} sun_amount={} moon_amount={}",
                    state.current_scene->area_weather.color_sun_fog & 0x00ffffffu,
                    state.current_scene->area_weather.color_moon_fog & 0x00ffffffu,
                    state.current_scene->area_weather.fog_clip_distance,
                    state.current_scene->area_weather.fog_sun_amount,
                    state.current_scene->area_weather.fog_moon_amount);
            }
        }
        break;
    case SDLK_H:
        state.shadow_debug_mode = (state.shadow_debug_mode + 1) % 2;
        LOG_F(INFO, "Shadow debug {}", state.shadow_debug_mode == 0 ? "off" : "cascades");
        break;
    case SDLK_J:
        state.gltf_ibl_strength = std::max(0.0f, state.gltf_ibl_strength - 0.1f * scale);
        LOG_F(INFO, "glTF IBL strength {:.2f}", state.gltf_ibl_strength);
        break;
    case SDLK_K:
        state.gltf_ibl_strength = std::min(4.0f, state.gltf_ibl_strength + 0.1f * scale);
        LOG_F(INFO, "glTF IBL strength {:.2f}", state.gltf_ibl_strength);
        break;
    case SDLK_N:
        state.gltf_exposure = std::max(0.1f, state.gltf_exposure - 0.1f * scale);
        LOG_F(INFO, "glTF exposure {:.2f}", state.gltf_exposure);
        break;
    case SDLK_M:
        state.gltf_exposure = std::min(10.0f, state.gltf_exposure + 0.1f * scale);
        LOG_F(INFO, "glTF exposure {:.2f}", state.gltf_exposure);
        break;
    case SDLK_F:
        if (state.current_scene) {
            if (state.area_navigation) {
                fit_area_navigation_camera(*state.camera, state.current_scene->current_bounds());
            } else {
                state.camera->fit_to_bounds(state.current_scene->current_bounds());
            }
        }
        break;
    case SDLK_A:
        if (state.area_navigation) {
            state.camera->move_right(-move_speed);
        } else {
            state.camera->orbit(-8.0f * scale, 0.0f);
        }
        break;
    case SDLK_D:
        if (state.area_navigation) {
            state.camera->move_right(move_speed);
        } else {
            state.camera->orbit(8.0f * scale, 0.0f);
        }
        break;
    case SDLK_W:
        if (state.area_navigation) {
            state.camera->move_forward(move_speed, true);
        } else {
            state.camera->orbit(0.0f, 6.0f * scale);
        }
        break;
    case SDLK_S:
        if (state.area_navigation) {
            state.camera->move_forward(-move_speed, true);
        } else {
            state.camera->orbit(0.0f, -6.0f * scale);
        }
        break;
    case SDLK_LEFT:
        if (state.area_navigation) {
            state.camera->yaw(-rotate_speed);
        } else {
            state.camera->orbit(-8.0f * scale, 0.0f);
        }
        break;
    case SDLK_RIGHT:
        if (state.area_navigation) {
            state.camera->yaw(rotate_speed);
        } else {
            state.camera->orbit(8.0f * scale, 0.0f);
        }
        break;
    case SDLK_UP:
        if (state.area_navigation) {
            if (key.mod & SDL_KMOD_CTRL) {
                state.camera->pitch(rotate_speed);
            } else {
                state.camera->move_up(move_speed);
            }
        } else {
            state.camera->orbit(0.0f, 6.0f * scale);
        }
        break;
    case SDLK_DOWN:
        if (state.area_navigation) {
            if (key.mod & SDL_KMOD_CTRL) {
                state.camera->pitch(-rotate_speed);
            } else {
                state.camera->move_up(-move_speed);
            }
        } else {
            state.camera->orbit(0.0f, -6.0f * scale);
        }
        break;
    case SDLK_Q:
        state.camera->zoom(1.0f + 0.12f * scale);
        break;
    case SDLK_E:
        state.camera->zoom(1.0f / (1.0f + 0.12f * scale));
        break;
    default:
        break;
    }
}

void render_frame(AppState& state)
{
    if (state.renderer_reload_requested) {
        state.renderer_reload_requested = false;
        reload_renderer_runtime(state);
    }

    auto* cmd = nw::gfx::begin_frame(state.gfx_context);
    if (!cmd) return;

    imgui_prepare_frame(state, cmd);

    nw::gfx::cmd_begin_render(cmd, {});

    nw::gfx::cmd_set_viewport(cmd,
        0.0f, 0.0f,
        static_cast<float>(state.window_width),
        static_cast<float>(state.window_height),
        0.0f, 1.0f);

    nw::gfx::cmd_set_scissor(cmd,
        0, 0,
        static_cast<uint32_t>(state.window_width),
        static_cast<uint32_t>(state.window_height));

    if (state.current_scene && state.renderer && state.camera) {
        const auto bounds = state.current_scene->current_bounds();
        const auto clip_planes = camera_clip_planes(*state.camera, bounds);
        state.camera->set_near_far(clip_planes.first, clip_planes.second);

        RenderContext ctx{};
        ctx.view = state.camera->get_view_matrix();
        ctx.projection = state.camera->get_projection_matrix();
        ctx.camera_position = state.camera->get_position();
        ctx.camera_target = state.camera->get_target();
        ctx.camera_near_plane = state.camera->near_plane();
        ctx.camera_far_plane = state.camera->far_plane();
        ctx.orthographic_camera = state.camera->is_orthographic();
        ctx.static_pbr_ibl_strength = state.gltf_ibl_strength;
        ctx.static_pbr_exposure = state.gltf_exposure;
        ctx.static_pbr_ibl_diffuse_texture_index = state.gltf_ibl_diffuse_texture_index;
        ctx.static_pbr_ibl_specular_texture_index = state.gltf_ibl_specular_texture_index;
        ctx.static_pbr_brdf_lut_texture_index = state.gltf_brdf_lut_texture_index;
        ctx.static_pbr_ibl_enabled = state.gltf_ibl_enabled;
        ctx.lighting = resolve_scene_lighting(state, *state.current_scene);
        ctx.lighting_space = resolve_scene_lighting_space(*state.current_scene);

        const auto fog = resolve_scene_fog(state, *state.current_scene);
        ctx.fog = fog;
        ctx.shadow = resolve_scene_shadow(ctx, bounds);
        ctx.shadow.debug_mode = state.shadow_debug_mode;

        if (ctx.shadow.enabled && state.renderer->shadow_pipeline_ready()
            && state.renderer->ensure_shadow_resources(kShadowMapResolution)) {
            nw::gfx::cmd_end_render(cmd);
            for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
                ctx.shadow.depth_textures[cascade] = state.renderer->shadow_depth_texture(cascade);
                nw::gfx::cmd_begin_render(cmd, state.renderer->shadow_render_target(cascade));
                nw::gfx::cmd_set_viewport(cmd, 0.0f, 0.0f,
                    static_cast<float>(kShadowMapResolution),
                    static_cast<float>(kShadowMapResolution),
                    0.0f, 1.0f);
                nw::gfx::cmd_set_scissor(cmd, 0, 0, kShadowMapResolution, kShadowMapResolution);
                for (const auto& model : state.current_scene->models) {
                    state.renderer->render_shadow_model(cmd, *model, glm::mat4(1.0f), ctx.shadow.world_to_shadow[cascade]);
                }
                nw::gfx::cmd_end_render(cmd);
            }

            nw::gfx::cmd_begin_render(cmd, {});
            nw::gfx::cmd_set_viewport(cmd,
                0.0f, 0.0f,
                static_cast<float>(state.window_width),
                static_cast<float>(state.window_height),
                0.0f, 1.0f);
            nw::gfx::cmd_set_scissor(cmd,
                0, 0,
                static_cast<uint32_t>(state.window_width),
                static_cast<uint32_t>(state.window_height));
        }
        for (const auto& model : state.current_scene->models) {
            state.renderer->render_model(cmd, *model, ctx);
        }
        for (size_t model_index = 0; model_index < state.current_scene->static_models.size(); ++model_index) {
            const auto& model = state.current_scene->static_models[model_index];
            std::vector<std::vector<glm::mat4>> skin_matrices;
            if (!model->animations.empty() && !model->skeletons.empty()) {
                const auto& clip = model->animations[state.gltf_animation_clip % model->animations.size()];
                if (clip.skeleton < model->skeletons.size()) {
                    nw::render::Pose& pose = state.gltf_poses[model_index];
                    bool sampled = false;
                    if (model_index < state.gltf_animation_backends.size() && state.gltf_animation_backends[model_index]) {
                        const uint32_t clip_idx = state.gltf_animation_clip % static_cast<uint32_t>(model->animations.size());
                        sampled = state.gltf_animation_backends[model_index]->sample(
                            clip_idx, state.gltf_animation_time, pose, true);
                    }
                    if (!sampled) {
                        sampled = nw::render::sample_clip(model->skeletons[clip.skeleton], clip, state.gltf_animation_time, pose, true);
                    }
                    if (sampled) {
                        nw::render::build_model_matrices(model->skeletons[clip.skeleton], pose);
                        skin_matrices.resize(model->skins.size());
                        skin_matrices[clip.skeleton].resize(model->skeletons[clip.skeleton].joints.size());
                        nw::render::build_skin_matrices(model->skeletons[clip.skeleton], pose, skin_matrices[clip.skeleton]);
                    }
                }
            }
            state.renderer->render_static_model(cmd, *model, ctx, RenderPassSelection::all,
                skin_matrices.empty() ? nullptr : &skin_matrices);
        }
        state.renderer->render_particles(cmd, *state.current_scene, ctx);
        if (state.show_debug_overlay) {
            state.renderer->render_debug_grid(cmd, *state.current_scene, ctx,
                state.debug_grid_spacing,
                state.debug_grid_major_interval,
                state.debug_grid_minor_width,
                state.debug_grid_major_width,
                state.debug_grid_opacity,
                state.debug_grid_z_offset);
        }
    }

    imgui_render(state, cmd);
    nw::gfx::cmd_end_render(cmd);

    if (state.screenshot_requested && state.screenshot_delay_frames > 0) {
        --state.screenshot_delay_frames;
    } else if (state.screenshot_requested || state.turntable_capture) {
        auto path = current_capture_path(state);
        bool captured = nw::gfx::capture_screenshot(state.gfx_context, cmd, path.c_str());
        if (captured) {
            LOG_F(INFO, "Saved screenshot: {}", path);
        } else {
            LOG_F(ERROR, "Failed to save screenshot: {}", path);
        }

        if (state.turntable_capture) {
            ++state.capture_index;
            if (state.capture_index >= state.turntable_frames) {
                state.running = false;
            }
        } else {
            state.screenshot_captured = captured;
            state.screenshot_requested = false;
            state.running = false;
        }
        return;
    }

    nw::gfx::end_frame(state.gfx_context);
}

void set_vfx_sequence_time(AppState& state, int time_ms, bool stop_after_seek)
{
    if (!state.current_scene || state.vfx_sequence_steps.empty() || state.vfx_sequence_loop_ms <= 0) {
        return;
    }

    const int clamped = std::clamp(time_ms, 0, state.vfx_sequence_loop_ms);
    state.vfx_sequence_loop_seed = 0x12345678u;
    reset_vfx_sequence(state);
    update_vfx_sequence(state, clamped);
    state.current_scene->update(clamped);
    if (stop_after_seek) {
        stop_scene_playback(state, false);
    }
}

int run_area_dump_command(const std::filesystem::path& module_path, const std::filesystem::path& output_dir,
    std::string_view user_path, bool skip_existing, size_t limit, bool show_debug_overlay)
{
    AppState state{};
    state.screenshot_delay_frames = 2;
    state.show_debug_overlay = show_debug_overlay;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_F(ERROR, "SDL_Init failed: {}", SDL_GetError());
        nw::kernel::services().shutdown();
        return 1;
    }

    if (!init_graphics(state, nullptr)) {
        LOG_F(ERROR, "Failed to initialize graphics for area dump");
        SDL_Quit();
        nw::kernel::services().shutdown();
        return 1;
    }

    state.camera = std::make_unique<Camera>();
    state.camera->set_aspect_ratio(1280.0f / 720.0f);

    nw::Module* module = nullptr;
    if (!init_kernel_services(module_path.string(), user_path, &module)) {
        shutdown_graphics(state);
        SDL_Quit();
        return 1;
    }

    if (!init_render_runtime(state)) {
        nw::kernel::services().shutdown();
        shutdown_graphics(state);
        SDL_Quit();
        return 1;
    }

    state.renderer = std::make_unique<Renderer>(state.gfx_context);
    if (!state.renderer->initialize(state.shader_provider.get())) {
        state.renderer.reset();
        state.shader_provider.reset();
        shutdown_graphics(state);
        SDL_Quit();
        nw::kernel::services().shutdown();
        return 1;
    }

    std::vector<nw::Resref> areas;
    if (module->areas.is<nw::Vector<nw::Resref>>()) {
        const auto& module_areas = module->areas.as<nw::Vector<nw::Resref>>();
        areas.assign(module_areas.begin(), module_areas.end());
    }
    if (areas.empty()) {
        LOG_F(ERROR, "Module has no areas: {}", module_path.string());
        state.renderer.reset();
        state.shader_provider.reset();
        shutdown_graphics(state);
        SDL_Quit();
        nw::kernel::services().shutdown();
        return 1;
    }

    const auto out_dir = output_dir.empty() ? std::filesystem::path{"."} : output_dir;
    std::filesystem::create_directories(out_dir);

    size_t processed = 0;
    for (const auto& area : areas) {
        if (limit > 0 && processed >= limit) {
            break;
        }

        const auto screenshot_path = (out_dir / (std::string(area.view()) + ".png"));
        if (skip_existing && std::filesystem::exists(screenshot_path)) {
            LOG_F(INFO, "Skipping existing area screenshot: {}", screenshot_path.string());
            continue;
        }

        load_area(state, area.view());
        if (!state.current_scene) {
            LOG_F(ERROR, "Failed to load area for dump: {}", area.view());
            continue;
        }

        state.screenshot_path = screenshot_path.string();
        state.screenshot_requested = true;
        state.screenshot_captured = false;
        state.screenshot_delay_frames = 2;
        state.running = true;

        for (int i = 0; i < 8 && !state.screenshot_captured; ++i) {
            render_frame(state);
        }

        if (!state.screenshot_captured) {
            LOG_F(ERROR, "Failed to capture area screenshot: {}", area.view());
        } else {
            LOG_F(INFO, "Captured area screenshot: {}", state.screenshot_path);
            ++processed;
        }
    }

    state.current_scene.reset();
    state.renderer.reset();
    state.shader_provider.reset();
    state.camera.reset();
    shutdown_graphics(state);
    SDL_Quit();
    nw::kernel::services().shutdown();
    return 0;
}

} // namespace mudl
