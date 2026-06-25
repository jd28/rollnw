#include "particle_system.hpp"

#include "../util/profile.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <xsimd/xsimd.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <utility>

namespace nw::render {

namespace {

glm::vec3 emitter_position(const ParticleEmitterState& state)
{
    return glm::vec3(state.world_transform[3]);
}

glm::vec3 transform_direction(const glm::mat4& transform, const glm::vec3& direction)
{
    const glm::vec3 world = glm::vec3(transform * glm::vec4(direction, 0.0f));
    const float length2 = glm::dot(world, world);
    if (length2 <= 1.0e-8f) {
        return direction;
    }
    return world / std::sqrt(length2);
}

uint32_t advance_rng(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

constexpr float kBeamPerpetualLifetime = 1.0e9f;

float random_unit_f32(uint32_t& state)
{
    return static_cast<float>(advance_rng(state) >> 8) * (1.0f / 16777216.0f);
}

float random_range_f32(uint32_t& state, float min, float max)
{
    if (max <= min) { return min; }
    return std::lerp(min, max, random_unit_f32(state));
}

bool is_projectile_body_sprite(const CompiledParticleEmitter& emitter);

bool should_disable_random_sheet_start_for_additive_flare(
    const CompiledParticleEmitter& emitter, const CompiledParticleMaterial* material, const ParticleSpriteSheet& sheet)
{
    if (!material || material->blend != ParticleBlendMode::additive) { return false; }
    if (emitter.render.mode != ParticleRenderMode::billboard) { return false; }
    if (!sheet.random_start) { return false; }
    if (emitter.region.type != ParticleSpawnRegionType::point) { return false; }
    if (emitter.initial.speed.max > 1.0e-6f) { return false; }
    if (emitter.initial.spread_radians > 1.0e-6f) { return false; }
    if (emitter.initial.rotation.min != 0.0f || emitter.initial.rotation.max != 0.0f) { return false; }
    if (emitter.initial.rotation_rate.min != 0.0f || emitter.initial.rotation_rate.max != 0.0f) { return false; }
    return true;
}

float sanitize_emission_rate(float value)
{
    if (!std::isfinite(value)) { return 0.0f; }
    return std::max(value, 0.0f);
}

glm::vec3 spawn_position(ParticleEmitterState& state, const CompiledParticleEmitter& emitter, const glm::vec3& base_position)
{
    glm::vec3 position = base_position;

    if (emitter.region.type == ParticleSpawnRegionType::rect) {
        const glm::vec3 local_offset{
            random_range_f32(state.random_seed, -0.5f * emitter.region.size.x * 0.01f, 0.5f * emitter.region.size.x * 0.01f),
            random_range_f32(state.random_seed, -0.5f * emitter.region.size.y * 0.01f, 0.5f * emitter.region.size.y * 0.01f),
            0.0f,
        };
        position += glm::vec3(state.world_transform * glm::vec4(local_offset, 0.0f));
    }

    return position;
}

uint32_t pack_color(const glm::vec4& color)
{
    const auto pack = [](float value) {
        return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    return pack(color.r) | (pack(color.g) << 8) | (pack(color.b) << 16) | (pack(color.a) << 24);
}

float eval_keyed_scalar_track(const CompiledParticleKeyedScalarTrack& track, float time);

const CompiledParticleMaterial* get_material(const CompiledParticleEffect& effect, uint32_t material_id)
{
    if (material_id >= effect.materials.size()) { return nullptr; }
    return &effect.materials[material_id];
}

ParticleSpriteSheet eval_emitter_sheet(const CompiledParticleEmitter& emitter, const CompiledParticleMaterial& material, float emitter_time)
{
    ParticleSpriteSheet sheet = material.sheet;
    const uint16_t max_frames = std::max<uint16_t>(1, static_cast<uint16_t>(sheet.columns * sheet.rows));
    const float sampled_frame_begin = std::max(0.0f, eval_keyed_scalar_track(emitter.sheet_frame_begin_track, emitter_time));
    const float sampled_frame_end = std::max(0.0f, eval_keyed_scalar_track(emitter.sheet_frame_end_track, emitter_time));
    const float sampled_fps = std::max(0.0f, eval_keyed_scalar_track(emitter.sheet_fps_track, emitter_time));
    const float sampled_random_start = eval_keyed_scalar_track(emitter.sheet_random_start_track, emitter_time);

    sheet.frame_begin = static_cast<uint16_t>(std::min<float>(sampled_frame_begin, static_cast<float>(max_frames - 1)));
    sheet.frame_end = static_cast<uint16_t>(std::min<float>(std::max(sampled_frame_begin, sampled_frame_end), static_cast<float>(max_frames - 1)));
    if (max_frames > 1 && sheet.frame_begin == 0 && sheet.frame_end == 0) {
        sheet.frame_end = static_cast<uint16_t>(max_frames - 1);
    }
    sheet.frames_per_second = sampled_fps;
    sheet.random_start = sampled_random_start >= 0.5f;
    if (should_disable_random_sheet_start_for_additive_flare(emitter, &material, sheet)) {
        sheet.random_start = false;
    }
    return sheet;
}

uint16_t initial_frame_offset(const ParticleSpriteSheet& sheet, ParticleEmitterState& state)
{
    const uint16_t frame_begin = sheet.frame_begin;
    const uint16_t frame_end = std::max(frame_begin, sheet.frame_end);
    const uint16_t frame_count = static_cast<uint16_t>(frame_end - frame_begin + 1);
    if (frame_count <= 1 || !sheet.random_start) { return 0; }
    return static_cast<uint16_t>(advance_rng(state.random_seed) % frame_count);
}

uint16_t eval_sprite_frame(const ParticleSpriteSheet& sheet, float age, float normalized_age, uint16_t frame_offset)
{
    const uint16_t frame_begin = sheet.frame_begin;
    const uint16_t frame_end = std::max(frame_begin, sheet.frame_end);
    const uint16_t frame_count = static_cast<uint16_t>(frame_end - frame_begin + 1);
    if (frame_count <= 1) { return frame_begin; }
    if (sheet.frames_per_second <= 0.0f) {
        const uint32_t base = static_cast<uint32_t>(frame_offset % frame_count);
        const float t = std::clamp(normalized_age, 0.0f, 0.99999994f);
        const uint32_t frame_index = std::min<uint32_t>(
            static_cast<uint32_t>(frame_count - 1),
            static_cast<uint32_t>(std::floor(t * static_cast<float>(frame_count))));
        return static_cast<uint16_t>(frame_begin + ((base + frame_index) % frame_count));
    }

    const uint32_t step = static_cast<uint32_t>(std::max(0.0f, std::floor(age * sheet.frames_per_second)));
    const uint32_t frame_index = (static_cast<uint32_t>(frame_offset) + step) % frame_count;
    return static_cast<uint16_t>(frame_begin + frame_index);
}

glm::vec3 initial_emission_direction(const CompiledParticleEmitter& emitter, ParticleEmitterState& state, float spread_radians)
{
    glm::vec3 direction = transform_direction(state.world_transform, glm::vec3{0.0f, 0.0f, 1.0f});
    if (spread_radians > 0.0f) {
        const float azimuth = random_range_f32(state.random_seed, 0.0f, 2.0f * std::numbers::pi_v<float>);
        const float theta = random_range_f32(state.random_seed, 0.0f, spread_radians);
        const float sin_theta = std::sin(theta);
        const glm::vec3 local_direction = glm::normalize(glm::vec3{
            std::cos(azimuth) * sin_theta,
            std::sin(azimuth) * sin_theta,
            std::cos(theta),
        });
        direction = transform_direction(state.world_transform, local_direction);
    }

    const float length2 = glm::dot(direction, direction);
    if (length2 > 1.0e-8f) {
        direction /= std::sqrt(length2);
    } else {
        direction = glm::vec3{0.0f, 0.0f, 1.0f};
    }

    return direction;
}

glm::vec3 initial_velocity(const CompiledParticleEmitter& emitter, ParticleEmitterState& state,
    const glm::vec3& emission_direction)
{
    const float emitter_time = state.time;
    const float base_speed = eval_keyed_scalar_track(emitter.speed_track, emitter_time);
    const float random_speed = std::max(0.0f, eval_keyed_scalar_track(emitter.speed_random_track, emitter_time));
    return emission_direction * random_range_f32(state.random_seed, base_speed, base_speed + random_speed)
        + state.linear_velocity * emitter.initial.velocity_inheritance;
}

glm::vec3 initial_render_direction(const CompiledParticleEmitter& emitter, const ParticleEmitterState& state, const glm::vec3& particle_velocity,
    const glm::vec3& emission_direction)
{
    const float particle_speed2 = glm::dot(particle_velocity, particle_velocity);
    if (particle_speed2 > 1.0e-8f) {
        return particle_velocity / std::sqrt(particle_speed2);
    }
    if (is_projectile_body_sprite(emitter)) {
        const float emitter_speed2 = glm::dot(state.linear_velocity, state.linear_velocity);
        if (emitter_speed2 > 1.0e-8f) {
            return state.linear_velocity / std::sqrt(emitter_speed2);
        }
        const float emission_dir2 = glm::dot(emission_direction, emission_direction);
        if (emission_dir2 > 1.0e-8f) {
            return emission_direction / std::sqrt(emission_dir2);
        }
    }

    if (emitter.render.mode == ParticleRenderMode::velocity_aligned) {
        glm::vec3 default_emission_direction = transform_direction(state.world_transform, glm::vec3{0.0f, 0.0f, 1.0f});
        const float default_dir2 = glm::dot(default_emission_direction, default_emission_direction);
        if (default_dir2 > 1.0e-8f) {
            default_emission_direction /= std::sqrt(default_dir2);
        } else {
            default_emission_direction = glm::vec3{0.0f, 0.0f, 1.0f};
        }

        const glm::vec3 direction_delta = emission_direction - default_emission_direction;
        if (glm::dot(direction_delta, direction_delta) > 1.0e-8f) {
            const float emission_dir2 = glm::dot(emission_direction, emission_direction);
            if (emission_dir2 > 1.0e-8f) {
                return emission_direction / std::sqrt(emission_dir2);
            }
        }
    }

    return glm::vec3{0.0f};
}

bool uses_projectile_spark_trail(const CompiledParticleEmitter& emitter, const CompiledParticleMaterial* material)
{
    return emitter.simulation_space == ParticleSimulationSpace::world
        && emitter.emission.metric == ParticleSpawnMetric::per_distance
        && emitter.render.mode == ParticleRenderMode::billboard
        && material
        && material->blend == ParticleBlendMode::additive
        && emitter.initial.speed.max > 1.0e-6f
        && emitter.initial.lifetime.max >= 0.5f;
}

bool is_projectile_body_sprite(const CompiledParticleEmitter& emitter)
{
    return emitter.render.semantic == ParticleRenderSemantic::projectile_body_sprite;
}

bool uses_triggered_motion_trail_attachment(const CompiledParticleEmitter& emitter)
{
    return emitter.simulation_space == ParticleSimulationSpace::emitter_attached
        && emitter.emission.trigger_on_effect_events
        && emitter.render.mode == ParticleRenderMode::velocity_aligned;
}

ParticleSimulationSpace effective_simulation_space(const CompiledParticleEmitter& emitter)
{
    if (is_projectile_body_sprite(emitter)
        && emitter.simulation_space == ParticleSimulationSpace::emitter_attached) {
        return ParticleSimulationSpace::spawn_attached;
    }
    if (uses_triggered_motion_trail_attachment(emitter)) {
        return ParticleSimulationSpace::spawn_attached;
    }
    return emitter.simulation_space;
}

glm::vec3 particle_anchor_position(const ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index)
{
    const auto& emitter = system.effect->emitters[emitter_id];
    switch (effective_simulation_space(emitter)) {
    case ParticleSimulationSpace::local:
    case ParticleSimulationSpace::emitter_attached:
        return emitter_position(system.emitters[emitter_id]);
    case ParticleSimulationSpace::spawn_attached:
        return system.particles.attachment.anchor_position[particle_index];
    case ParticleSimulationSpace::world:
        return glm::vec3{0.0f};
    }

    return glm::vec3{0.0f};
}

glm::vec3 particle_anchor_position(const ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index)
{
    switch (effective_simulation_space(emitter)) {
    case ParticleSimulationSpace::local:
    case ParticleSimulationSpace::emitter_attached:
        return emitter_position(emitter_state);
    case ParticleSimulationSpace::spawn_attached:
        return system.particles.attachment.anchor_position[particle_index];
    case ParticleSimulationSpace::world:
        return glm::vec3{0.0f};
    }

    return glm::vec3{0.0f};
}

glm::vec3 attachment_local_to_world(const ParticleEmitterState& state, ParticleSimulationSpace space,
    const glm::vec3& anchor_position, const glm::vec3& local_position)
{
    switch (space) {
    case ParticleSimulationSpace::local:
    case ParticleSimulationSpace::emitter_attached:
        return glm::vec3(state.world_transform * glm::vec4(local_position, 1.0f));
    case ParticleSimulationSpace::spawn_attached:
        return anchor_position + local_position;
    case ParticleSimulationSpace::world:
        return local_position;
    }

    return local_position;
}

glm::vec3 attachment_world_to_local(const ParticleEmitterState& state, ParticleSimulationSpace space,
    const glm::vec3& anchor_position, const glm::vec3& world_position)
{
    switch (space) {
    case ParticleSimulationSpace::local:
    case ParticleSimulationSpace::emitter_attached:
        return glm::vec3(state.inverse_world_transform * glm::vec4(world_position, 1.0f));
    case ParticleSimulationSpace::spawn_attached:
        return world_position - anchor_position;
    case ParticleSimulationSpace::world:
        return world_position;
    }

    return world_position;
}

glm::vec3 attachment_world_delta_to_local(const ParticleEmitterState& state, ParticleSimulationSpace space,
    const glm::vec3& world_delta)
{
    switch (space) {
    case ParticleSimulationSpace::local:
    case ParticleSimulationSpace::emitter_attached:
        return glm::vec3(state.inverse_world_transform * glm::vec4(world_delta, 0.0f));
    case ParticleSimulationSpace::spawn_attached:
    case ParticleSimulationSpace::world:
        return world_delta;
    }

    return world_delta;
}

void set_particle_world_position(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index, const glm::vec3& position)
{
    auto& emitter = system.effect->emitters[emitter_id];
    const auto simulation_space = effective_simulation_space(emitter);
    if (simulation_space == ParticleSimulationSpace::world) {
        system.particles.core.position[particle_index] = position;
        return;
    }

    const glm::vec3 anchor = particle_anchor_position(system, emitter_id, particle_index);
    const auto& emitter_state = system.emitters[emitter_id];
    system.particles.attachment.local_position[particle_index] =
        attachment_world_to_local(emitter_state, simulation_space, anchor, position);
    system.particles.core.position[particle_index] = position;
}

void set_particle_world_position(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index, const glm::vec3& position)
{
    const auto simulation_space = effective_simulation_space(emitter);
    if (simulation_space == ParticleSimulationSpace::world) {
        system.particles.core.position[particle_index] = position;
        return;
    }

    const glm::vec3 anchor = particle_anchor_position(system, emitter, emitter_state, particle_index);
    system.particles.attachment.local_position[particle_index] =
        attachment_world_to_local(emitter_state, simulation_space, anchor, position);
    system.particles.core.position[particle_index] = position;
}

void sync_attached_particle_position(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index)
{
    const auto& emitter = system.effect->emitters[emitter_id];
    const auto simulation_space = effective_simulation_space(emitter);
    if (simulation_space == ParticleSimulationSpace::world) { return; }

    const auto& emitter_state = system.emitters[emitter_id];
    const glm::vec3 anchor = particle_anchor_position(system, emitter_id, particle_index);
    system.particles.core.position[particle_index] = attachment_local_to_world(
        emitter_state, simulation_space, anchor, system.particles.attachment.local_position[particle_index]);
}

void sync_attached_particle_position(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index)
{
    const auto simulation_space = effective_simulation_space(emitter);
    if (simulation_space == ParticleSimulationSpace::world) { return; }

    const glm::vec3 anchor = particle_anchor_position(system, emitter, emitter_state, particle_index);
    system.particles.core.position[particle_index] = attachment_local_to_world(
        emitter_state, simulation_space, anchor, system.particles.attachment.local_position[particle_index]);
}

bool apply_particle_collision(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index, const glm::vec3& previous_position,
    const ParticleSimulationContext& context)
{
    const auto& emitter = system.effect->emitters[emitter_id];
    if (!emitter.collision.enabled || !context.collision) { return false; }

    const float radius = 0.5f * std::max(system.particles.core.size_x[particle_index], system.particles.core.size_y[particle_index]);
    const auto hit = context.collision->trace_particle(previous_position, system.particles.core.position[particle_index], radius);
    if (!hit.hit) { return false; }

    set_particle_world_position(system, emitter_id, particle_index, hit.position);

    if (emitter.collision.splat) {
        system.particles.core.velocity[particle_index] = glm::vec3{0.0f};
        system.particles.core.age[particle_index] = system.particles.core.lifetime[particle_index];
        return true;
    }

    if (emitter.collision.bounce) {
        const glm::vec3 normal = glm::dot(hit.normal, hit.normal) > 1.0e-8f
            ? glm::normalize(hit.normal)
            : glm::vec3{0.0f, 1.0f, 0.0f};
        system.particles.core.velocity[particle_index] = glm::reflect(system.particles.core.velocity[particle_index], normal)
            * emitter.collision.bounce_coefficient;
    } else {
        system.particles.core.velocity[particle_index] = glm::vec3{0.0f};
    }

    return true;
}

bool apply_particle_collision(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index, const glm::vec3& previous_position,
    const ParticleSimulationContext& context)
{
    if (!emitter.collision.enabled || !context.collision) { return false; }

    const float radius = 0.5f * std::max(system.particles.core.size_x[particle_index], system.particles.core.size_y[particle_index]);
    const auto hit = context.collision->trace_particle(previous_position, system.particles.core.position[particle_index], radius);
    if (!hit.hit) { return false; }

    set_particle_world_position(system, emitter, emitter_state, particle_index, hit.position);

    if (emitter.collision.splat) {
        system.particles.core.velocity[particle_index] = glm::vec3{0.0f};
        system.particles.core.age[particle_index] = system.particles.core.lifetime[particle_index];
        return true;
    }

    if (emitter.collision.bounce) {
        const glm::vec3 normal = glm::dot(hit.normal, hit.normal) > 1.0e-8f
            ? glm::normalize(hit.normal)
            : glm::vec3{0.0f, 1.0f, 0.0f};
        system.particles.core.velocity[particle_index] = glm::reflect(system.particles.core.velocity[particle_index], normal)
            * emitter.collision.bounce_coefficient;
    } else {
        system.particles.core.velocity[particle_index] = glm::vec3{0.0f};
    }

    return true;
}

glm::vec3 bezier_target_direction(const glm::vec3& source, const glm::vec3& target)
{
    glm::vec3 direction = target - source;
    const float length = glm::length(direction);
    if (length > 1.0e-6f) {
        direction /= length;
    } else {
        direction = glm::vec3{0.0f, 0.0f, 1.0f};
    }
    return direction;
}

void swap_core_particle(ParticleCoreStorage& core, size_t a, size_t b)
{
    std::swap(core.position[a], core.position[b]);
    std::swap(core.velocity[a], core.velocity[b]);
    std::swap(core.render_direction[a], core.render_direction[b]);
    std::swap(core.age[a], core.age[b]);
    std::swap(core.lifetime[a], core.lifetime[b]);
    std::swap(core.mass[a], core.mass[b]);
    std::swap(core.size_x[a], core.size_x[b]);
    std::swap(core.size_y[a], core.size_y[b]);
    std::swap(core.size_x_begin[a], core.size_x_begin[b]);
    std::swap(core.size_x_end[a], core.size_x_end[b]);
    std::swap(core.size_y_begin[a], core.size_y_begin[b]);
    std::swap(core.size_y_end[a], core.size_y_end[b]);
    std::swap(core.rotation[a], core.rotation[b]);
    std::swap(core.rotation_rate[a], core.rotation_rate[b]);
    std::swap(core.frame[a], core.frame[b]);
    std::swap(core.frame_offset[a], core.frame_offset[b]);
    std::swap(core.color_rgba8[a], core.color_rgba8[b]);
    std::swap(core.color_begin[a], core.color_begin[b]);
    std::swap(core.color_end[a], core.color_end[b]);
    std::swap(core.normalized_age[a], core.normalized_age[b]);
    std::swap(core.emitter_id[a], core.emitter_id[b]);
    std::swap(core.material_id[a], core.material_id[b]);
}

void copy_core_particle(ParticleCoreStorage& core, size_t dst, size_t src)
{
    if (dst == src) { return; }

    core.position[dst] = core.position[src];
    core.velocity[dst] = core.velocity[src];
    core.render_direction[dst] = core.render_direction[src];
    core.age[dst] = core.age[src];
    core.lifetime[dst] = core.lifetime[src];
    core.mass[dst] = core.mass[src];
    core.size_x[dst] = core.size_x[src];
    core.size_y[dst] = core.size_y[src];
    core.size_x_begin[dst] = core.size_x_begin[src];
    core.size_x_end[dst] = core.size_x_end[src];
    core.size_y_begin[dst] = core.size_y_begin[src];
    core.size_y_end[dst] = core.size_y_end[src];
    core.rotation[dst] = core.rotation[src];
    core.rotation_rate[dst] = core.rotation_rate[src];
    core.frame[dst] = core.frame[src];
    core.frame_offset[dst] = core.frame_offset[src];
    core.color_rgba8[dst] = core.color_rgba8[src];
    core.color_begin[dst] = core.color_begin[src];
    core.color_end[dst] = core.color_end[src];
    core.normalized_age[dst] = core.normalized_age[src];
    core.emitter_id[dst] = core.emitter_id[src];
    core.material_id[dst] = core.material_id[src];
}

void resize_core_storage(ParticleCoreStorage& core, size_t live)
{
    core.position.resize(live);
    core.velocity.resize(live);
    core.render_direction.resize(live);
    core.age.resize(live);
    core.lifetime.resize(live);
    core.mass.resize(live);
    core.size_x.resize(live);
    core.size_y.resize(live);
    core.size_x_begin.resize(live);
    core.size_x_end.resize(live);
    core.size_y_begin.resize(live);
    core.size_y_end.resize(live);
    core.rotation.resize(live);
    core.rotation_rate.resize(live);
    core.frame.resize(live);
    core.frame_offset.resize(live);
    core.color_rgba8.resize(live);
    core.color_begin.resize(live);
    core.color_end.resize(live);
    core.normalized_age.resize(live);
    core.emitter_id.resize(live);
    core.material_id.resize(live);
}

void reserve_core_storage(ParticleCoreStorage& core, size_t capacity)
{
    core.position.reserve(capacity);
    core.velocity.reserve(capacity);
    core.render_direction.reserve(capacity);
    core.age.reserve(capacity);
    core.lifetime.reserve(capacity);
    core.mass.reserve(capacity);
    core.size_x.reserve(capacity);
    core.size_y.reserve(capacity);
    core.size_x_begin.reserve(capacity);
    core.size_x_end.reserve(capacity);
    core.size_y_begin.reserve(capacity);
    core.size_y_end.reserve(capacity);
    core.rotation.reserve(capacity);
    core.rotation_rate.reserve(capacity);
    core.frame.reserve(capacity);
    core.frame_offset.reserve(capacity);
    core.color_rgba8.reserve(capacity);
    core.color_begin.reserve(capacity);
    core.color_end.reserve(capacity);
    core.normalized_age.reserve(capacity);
    core.emitter_id.reserve(capacity);
    core.material_id.reserve(capacity);
}

void resize_particle_sidecars(ParticleStorage& particles, size_t live)
{
    if (particles.has_bezier) {
        particles.bezier.source_position.resize(live);
        particles.bezier.target_position.resize(live);
        particles.bezier.source_tangent.resize(live);
        particles.bezier.target_tangent.resize(live);
    }
    if (particles.has_beams) {
        particles.beams.source_position.resize(live);
        particles.beams.target_position.resize(live);
        particles.beams.subdivisions.resize(live);
        particles.beams.update_accumulator.resize(live);
        particles.beams.jitter_radius.resize(live);
        particles.beams.noise_scale.resize(live);
    }
    if (particles.has_attachment) {
        particles.attachment.anchor_position.resize(live);
        particles.attachment.local_position.resize(live);
    }
}

void reserve_particle_sidecars(ParticleStorage& particles, size_t capacity)
{
    if (particles.has_bezier) {
        particles.bezier.source_position.reserve(capacity);
        particles.bezier.target_position.reserve(capacity);
        particles.bezier.source_tangent.reserve(capacity);
        particles.bezier.target_tangent.reserve(capacity);
    }
    if (particles.has_beams) {
        particles.beams.source_position.reserve(capacity);
        particles.beams.target_position.reserve(capacity);
        particles.beams.subdivisions.reserve(capacity);
        particles.beams.update_accumulator.reserve(capacity);
        particles.beams.jitter_radius.reserve(capacity);
        particles.beams.noise_scale.reserve(capacity);
    }
    if (particles.has_attachment) {
        particles.attachment.anchor_position.reserve(capacity);
        particles.attachment.local_position.reserve(capacity);
    }
}

void set_attachment_state(ParticleStorage& particles, size_t particle_index,
    const CompiledParticleEmitter& emitter, const ParticleEmitterState& state,
    const glm::vec3& base_position, const glm::vec3& world_position)
{
    if (!particles.has_attachment) { return; }

    switch (effective_simulation_space(emitter)) {
    case ParticleSimulationSpace::local:
    case ParticleSimulationSpace::emitter_attached: {
        const glm::vec3 anchor = emitter_position(state);
        particles.attachment.anchor_position[particle_index] = anchor;
        particles.attachment.local_position[particle_index] =
            attachment_world_to_local(state, emitter.simulation_space, anchor, world_position);
        break;
    }
    case ParticleSimulationSpace::spawn_attached:
        particles.attachment.anchor_position[particle_index] = base_position;
        particles.attachment.local_position[particle_index] = world_position - base_position;
        break;
    case ParticleSimulationSpace::world:
        particles.attachment.anchor_position[particle_index] = glm::vec3{0.0f};
        particles.attachment.local_position[particle_index] = world_position;
        break;
    }
}

void set_bezier_state(ParticleStorage& particles, size_t particle_index,
    const CompiledParticleEmitter& emitter, const ParticleEmitterState& state, const glm::vec3& world_position)
{
    if (!particles.has_bezier) { return; }

    particles.bezier.source_position[particle_index] = world_position;
    particles.bezier.target_position[particle_index] = state.target_point;
    const glm::vec3 target_dir = bezier_target_direction(world_position, state.target_point);

    particles.bezier.source_tangent[particle_index] = world_position + target_dir * emitter.targeting.source_tangent;
    particles.bezier.target_tangent[particle_index] = state.target_point - target_dir * emitter.targeting.target_tangent;
}

void set_beam_state(ParticleStorage& particles, size_t particle_index,
    const CompiledParticleEmitter& emitter, const ParticleEmitterState& state, const glm::vec3& world_position)
{
    if (!particles.has_beams) { return; }

    particles.beams.source_position[particle_index] = world_position;
    particles.beams.target_position[particle_index] = state.target_point;
    particles.beams.subdivisions[particle_index] = std::max<uint16_t>(
        1, emitter.beam.subdivisions == 0 ? static_cast<uint16_t>(std::max(1.0f, emitter.emission.rate)) : emitter.beam.subdivisions);
    particles.beams.update_accumulator[particle_index] = 0.0f;
    particles.beams.jitter_radius[particle_index] = emitter.beam.jitter_radius;
    particles.beams.noise_scale[particle_index] = emitter.beam.noise_scale;
}

void swap_particle(ParticleStorage& particles, size_t a, size_t b)
{
    if (a == b) { return; }

    swap_core_particle(particles.core, a, b);

    if (particles.has_bezier) {
        std::swap(particles.bezier.source_position[a], particles.bezier.source_position[b]);
        std::swap(particles.bezier.target_position[a], particles.bezier.target_position[b]);
        std::swap(particles.bezier.source_tangent[a], particles.bezier.source_tangent[b]);
        std::swap(particles.bezier.target_tangent[a], particles.bezier.target_tangent[b]);
    }
    if (particles.has_beams) {
        std::swap(particles.beams.source_position[a], particles.beams.source_position[b]);
        std::swap(particles.beams.target_position[a], particles.beams.target_position[b]);
        std::swap(particles.beams.subdivisions[a], particles.beams.subdivisions[b]);
        std::swap(particles.beams.update_accumulator[a], particles.beams.update_accumulator[b]);
        std::swap(particles.beams.jitter_radius[a], particles.beams.jitter_radius[b]);
        std::swap(particles.beams.noise_scale[a], particles.beams.noise_scale[b]);
    }
    if (particles.has_attachment) {
        std::swap(particles.attachment.anchor_position[a], particles.attachment.anchor_position[b]);
        std::swap(particles.attachment.local_position[a], particles.attachment.local_position[b]);
    }
}

void copy_particle(ParticleStorage& particles, size_t dst, size_t src)
{
    if (dst == src) { return; }

    copy_core_particle(particles.core, dst, src);

    if (particles.has_bezier) {
        particles.bezier.source_position[dst] = particles.bezier.source_position[src];
        particles.bezier.target_position[dst] = particles.bezier.target_position[src];
        particles.bezier.source_tangent[dst] = particles.bezier.source_tangent[src];
        particles.bezier.target_tangent[dst] = particles.bezier.target_tangent[src];
    }
    if (particles.has_beams) {
        particles.beams.source_position[dst] = particles.beams.source_position[src];
        particles.beams.target_position[dst] = particles.beams.target_position[src];
        particles.beams.subdivisions[dst] = particles.beams.subdivisions[src];
        particles.beams.update_accumulator[dst] = particles.beams.update_accumulator[src];
        particles.beams.jitter_radius[dst] = particles.beams.jitter_radius[src];
        particles.beams.noise_scale[dst] = particles.beams.noise_scale[src];
    }
    if (particles.has_attachment) {
        particles.attachment.anchor_position[dst] = particles.attachment.anchor_position[src];
        particles.attachment.local_position[dst] = particles.attachment.local_position[src];
    }
}

void remove_dead_particles(ParticleStorage& particles, std::span<uint32_t> live_particles_per_emitter)
{
    NW_PROFILE_SCOPE_N("remove_dead_particles");
    auto& core = particles.core;
    const size_t count = core.age.size();
    size_t write = 0;

    for (size_t read = 0; read < count; ++read) {
        if (core.age[read] < core.lifetime[read]) {
            copy_particle(particles, write, read);
            ++write;
            continue;
        }

        --live_particles_per_emitter[core.emitter_id[read]];
    }

    resize_core_storage(core, write);
    resize_particle_sidecars(particles, write);
}

template <typename Key, typename Mixer>
auto eval_track_keys(const std::vector<Key>& keys, float time, Mixer mix)
{
    const auto& first = keys.front();
    if (keys.size() == 1 || time <= first.time) { return first.value; }

    const auto& last = keys.back();
    if (time >= last.time) { return last.value; }

    const auto upper = std::upper_bound(keys.begin(), keys.end(), time, [](float lhs, const Key& rhs) {
        return lhs < rhs.time;
    });
    const auto& a = *(upper - 1);
    const auto& b = *upper;
    if (b.time <= a.time) { return a.value; }

    const float alpha = (time - a.time) / (b.time - a.time);
    return mix(a.value, b.value, alpha);
}

float eval_scalar_track(const CompiledParticleScalarTrack& track, float t)
{
    return eval_track_keys(track.keys, t, [](float a, float b, float alpha) {
        return std::lerp(a, b, alpha);
    });
}

glm::vec4 eval_color_track(const CompiledParticleColorTrack& track, float t)
{
    return eval_track_keys(track.keys, t, [](const glm::vec4& a, const glm::vec4& b, float alpha) {
        return glm::mix(a, b, alpha);
    });
}

float eval_keyed_scalar_track(const CompiledParticleKeyedScalarTrack& track, float time)
{
    return eval_track_keys(track.keys, time, [](float a, float b, float alpha) {
        return std::lerp(a, b, alpha);
    });
}

glm::vec4 eval_keyed_color_track(const CompiledParticleKeyedColorTrack& track, float time)
{
    return eval_track_keys(track.keys, time, [](const glm::vec4& a, const glm::vec4& b, float alpha) {
        return glm::mix(a, b, alpha);
    });
}

glm::vec3 cubic_bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t)
{
    const float inv = 1.0f - t;
    const float inv2 = inv * inv;
    const float inv3 = inv2 * inv;
    const float t2 = t * t;
    const float t3 = t2 * t;
    return inv3 * p0 + 3.0f * inv2 * t * p1 + 3.0f * inv * t2 * p2 + t3 * p3;
}

float eval_path_progress(float normalized_age, float age, float transition_factor)
{
    if (transition_factor <= 0.0f) { return normalized_age; }
    return std::clamp(age / transition_factor, 0.0f, 1.0f);
}

struct SimdScalarTrack {
    float begin = 0.0f;
    float end = 0.0f;
    bool linear = false;
};

struct BasicParticleSimdUpdate {
    SimdScalarTrack size_x;
    SimdScalarTrack size_y;
    bool update_size = false;
    bool spawn_size_x_lerp = false;
    bool spawn_size_y_lerp = false;
};

bool make_simd_scalar_track(const CompiledParticleScalarTrack& track, SimdScalarTrack& out)
{
    if (track.keys.size() == 1) {
        out = SimdScalarTrack{.begin = track.keys.front().value, .end = track.keys.front().value};
        return true;
    }

    if (track.keys.size() == 2 && track.keys[0].time == 0.0f && track.keys[1].time == 1.0f) {
        out = SimdScalarTrack{.begin = track.keys[0].value, .end = track.keys[1].value, .linear = true};
        return true;
    }

    return false;
}

// This fast path is deliberately narrow. Particle order can group an emitter
// without making storage contiguous, and attached/colliding particles need
// per-particle side effects that belong on the scalar path.
bool make_basic_particle_simd_update(const CompiledParticleEmitter& emitter,
    const ParticleSimulationContext& context, BasicParticleSimdUpdate& out)
{
    if (effective_simulation_space(emitter) != ParticleSimulationSpace::world) { return false; }
    if (emitter.affected_by_wind) { return false; }
    if (context.collision && emitter.collision.enabled) { return false; }

    constexpr uint32_t unsupported_update_features = CompiledParticleFeature::update_color
        | CompiledParticleFeature::update_rotation
        | CompiledParticleFeature::update_frame;
    if ((emitter.features & unsupported_update_features) != 0) { return false; }

    BasicParticleSimdUpdate result;
    result.update_size = (emitter.features & CompiledParticleFeature::update_size) != 0;
    result.spawn_size_x_lerp = (emitter.features & CompiledParticleFeature::spawn_size_x_lerp) != 0;
    result.spawn_size_y_lerp = (emitter.features & CompiledParticleFeature::spawn_size_y_lerp) != 0;

    if (result.update_size) {
        if (!result.spawn_size_x_lerp && !make_simd_scalar_track(emitter.size_x_track, result.size_x)) {
            return false;
        }
        if (!result.spawn_size_y_lerp && !make_simd_scalar_track(emitter.size_y_track, result.size_y)) {
            return false;
        }
    }

    out = result;
    return true;
}

bool particle_order_is_contiguous(std::span<const uint32_t> order, uint32_t begin, uint32_t end, size_t& first)
{
    if (begin >= end) { return false; }
    first = order[begin];
    for (uint32_t i = begin + 1; i < end; ++i) {
        if (order[i] != first + static_cast<size_t>(i - begin)) {
            return false;
        }
    }
    return true;
}

void store_simd_track(std::vector<float>& values, size_t offset, xsimd::batch<float> normalized_age,
    const SimdScalarTrack& track)
{
    using batch_type = xsimd::batch<float>;
    const batch_type result = track.linear
        ? batch_type::broadcast(track.begin) + (batch_type::broadcast(track.end - track.begin) * normalized_age)
        : batch_type::broadcast(track.begin);
    result.store_unaligned(values.data() + offset);
}

void store_simd_lerp(std::vector<float>& values, std::span<const float> begin_values, std::span<const float> end_values,
    size_t offset, xsimd::batch<float> normalized_age)
{
    using batch_type = xsimd::batch<float>;
    const batch_type begin = xsimd::load_unaligned(begin_values.data() + offset);
    const batch_type end = xsimd::load_unaligned(end_values.data() + offset);
    (begin + (end - begin) * normalized_age).store_unaligned(values.data() + offset);
}

void update_basic_particles_simd(ParticleCoreStorage& core, const BasicParticleSimdUpdate& update,
    size_t first, size_t count, float dt)
{
    using batch_type = xsimd::batch<float>;
    const size_t end = first + count;
    const size_t batch_size = batch_type::size;
    const size_t simd_end = first + ((count / batch_size) * batch_size);
    const batch_type dt_batch = batch_type::broadcast(dt);
    const batch_type min_lifetime = batch_type::broadcast(1.0e-6f);
    const batch_type zero = batch_type::broadcast(0.0f);
    const batch_type one = batch_type::broadcast(1.0f);

    size_t i = first;
    for (; i < simd_end; i += batch_size) {
        const batch_type age = xsimd::load_unaligned(core.age.data() + i) + dt_batch;
        const batch_type lifetime = xsimd::max(xsimd::load_unaligned(core.lifetime.data() + i), min_lifetime);
        const batch_type normalized_age = xsimd::min(xsimd::max(age / lifetime, zero), one);

        age.store_unaligned(core.age.data() + i);
        normalized_age.store_unaligned(core.normalized_age.data() + i);

        if (update.update_size) {
            if (update.spawn_size_x_lerp) {
                store_simd_lerp(core.size_x, core.size_x_begin, core.size_x_end, i, normalized_age);
            } else {
                store_simd_track(core.size_x, i, normalized_age, update.size_x);
            }
            if (update.spawn_size_y_lerp) {
                store_simd_lerp(core.size_y, core.size_y_begin, core.size_y_end, i, normalized_age);
            } else {
                store_simd_track(core.size_y, i, normalized_age, update.size_y);
            }
        }
    }

    for (; i < end; ++i) {
        core.age[i] += dt;
        const float lifetime = std::max(core.lifetime[i], 1.0e-6f);
        const float normalized_age = std::clamp(core.age[i] / lifetime, 0.0f, 1.0f);
        core.normalized_age[i] = normalized_age;

        if (update.update_size) {
            core.size_x[i] = update.spawn_size_x_lerp
                ? std::lerp(core.size_x_begin[i], core.size_x_end[i], normalized_age)
                : (update.size_x.linear ? std::lerp(update.size_x.begin, update.size_x.end, normalized_age) : update.size_x.begin);
            core.size_y[i] = update.spawn_size_y_lerp
                ? std::lerp(core.size_y_begin[i], core.size_y_end[i], normalized_age)
                : (update.size_y.linear ? std::lerp(update.size_y.begin, update.size_y.end, normalized_age) : update.size_y.begin);
        }
    }

    for (i = first; i < end; ++i) {
        core.velocity[i].z -= core.mass[i] * dt;
        core.position[i] += core.velocity[i] * dt;
    }
}

void update_particle_presentation(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index)
{
    auto& particles = system.particles;
    auto& core = particles.core;
    const auto& emitter = system.effect->emitters[emitter_id];

    const float lifetime = std::max(core.lifetime[particle_index], 1.0e-6f);
    const float normalized_age = std::clamp(core.age[particle_index] / lifetime, 0.0f, 1.0f);
    core.normalized_age[particle_index] = normalized_age;

    if ((emitter.features & CompiledParticleFeature::update_size) != 0) {
        core.size_x[particle_index] = (emitter.features & CompiledParticleFeature::spawn_size_x_lerp) != 0
            ? std::lerp(core.size_x_begin[particle_index], core.size_x_end[particle_index], normalized_age)
            : eval_scalar_track(emitter.size_x_track, normalized_age);
        core.size_y[particle_index] = (emitter.features & CompiledParticleFeature::spawn_size_y_lerp) != 0
            ? std::lerp(core.size_y_begin[particle_index], core.size_y_end[particle_index], normalized_age)
            : eval_scalar_track(emitter.size_y_track, normalized_age);
    }

    if ((emitter.features & CompiledParticleFeature::update_rotation) != 0) {
        core.rotation[particle_index] = eval_scalar_track(emitter.rotation_track, normalized_age)
            + core.rotation_rate[particle_index] * core.age[particle_index];
    }

    if ((emitter.features & CompiledParticleFeature::update_frame) != 0) {
        core.frame[particle_index] = eval_sprite_frame(
            system.emitters[emitter_id].sampled_sheet, core.age[particle_index], normalized_age, core.frame_offset[particle_index]);
    }

    if ((emitter.features & CompiledParticleFeature::update_color) != 0) {
        glm::vec4 color = (emitter.features & CompiledParticleFeature::spawn_color_lerp) != 0
            ? glm::mix(core.color_begin[particle_index], core.color_end[particle_index], normalized_age)
            : eval_color_track(emitter.color_track, normalized_age);
        if ((emitter.features & CompiledParticleFeature::spawn_color_lerp) == 0) {
            color.a = eval_scalar_track(emitter.alpha_track, normalized_age);
        }
        core.color_rgba8[particle_index] = pack_color(color);
    }
}

void update_particle_presentation(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index)
{
    auto& particles = system.particles;
    auto& core = particles.core;

    const float lifetime = std::max(core.lifetime[particle_index], 1.0e-6f);
    const float normalized_age = std::clamp(core.age[particle_index] / lifetime, 0.0f, 1.0f);
    core.normalized_age[particle_index] = normalized_age;

    if ((emitter.features & CompiledParticleFeature::update_size) != 0) {
        core.size_x[particle_index] = (emitter.features & CompiledParticleFeature::spawn_size_x_lerp) != 0
            ? std::lerp(core.size_x_begin[particle_index], core.size_x_end[particle_index], normalized_age)
            : eval_scalar_track(emitter.size_x_track, normalized_age);
        core.size_y[particle_index] = (emitter.features & CompiledParticleFeature::spawn_size_y_lerp) != 0
            ? std::lerp(core.size_y_begin[particle_index], core.size_y_end[particle_index], normalized_age)
            : eval_scalar_track(emitter.size_y_track, normalized_age);
    }

    if ((emitter.features & CompiledParticleFeature::update_rotation) != 0) {
        core.rotation[particle_index] = eval_scalar_track(emitter.rotation_track, normalized_age)
            + core.rotation_rate[particle_index] * core.age[particle_index];
    }

    if ((emitter.features & CompiledParticleFeature::update_frame) != 0) {
        core.frame[particle_index] = eval_sprite_frame(
            emitter_state.sampled_sheet, core.age[particle_index], normalized_age, core.frame_offset[particle_index]);
    }

    if ((emitter.features & CompiledParticleFeature::update_color) != 0) {
        glm::vec4 color = (emitter.features & CompiledParticleFeature::spawn_color_lerp) != 0
            ? glm::mix(core.color_begin[particle_index], core.color_end[particle_index], normalized_age)
            : eval_color_track(emitter.color_track, normalized_age);
        if ((emitter.features & CompiledParticleFeature::spawn_color_lerp) == 0) {
            color.a = eval_scalar_track(emitter.alpha_track, normalized_age);
        }
        core.color_rgba8[particle_index] = pack_color(color);
    }
}

void advance_attached_bezier_path(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index)
{
    const auto& emitter = system.effect->emitters[emitter_id];
    if (emitter.simulation_space != ParticleSimulationSpace::local
        && emitter.simulation_space != ParticleSimulationSpace::emitter_attached) {
        return;
    }

    const glm::vec3 delta = system.emitters[emitter_id].frame_delta;
    auto& bezier = system.particles.bezier;
    bezier.source_position[particle_index] += delta;
    bezier.source_tangent[particle_index] += delta;
    bezier.target_tangent[particle_index] += delta;
    bezier.target_position[particle_index] += delta;
}

void advance_attached_bezier_path(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index)
{
    if (emitter.simulation_space != ParticleSimulationSpace::local
        && emitter.simulation_space != ParticleSimulationSpace::emitter_attached) {
        return;
    }

    const glm::vec3 delta = emitter_state.frame_delta;
    auto& bezier = system.particles.bezier;
    bezier.source_position[particle_index] += delta;
    bezier.source_tangent[particle_index] += delta;
    bezier.target_tangent[particle_index] += delta;
    bezier.target_position[particle_index] += delta;
}

void sync_dynamic_particle_position(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index)
{
    sync_attached_particle_position(system, emitter_id, particle_index);
}

void sync_dynamic_particle_position(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index)
{
    sync_attached_particle_position(system, emitter, emitter_state, particle_index);
}

void advance_dynamic_particle(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index, float dt,
    const ParticleSimulationContext& context)
{
    auto& particles = system.particles;
    auto& core = particles.core;
    const auto& emitter = system.effect->emitters[emitter_id];

    sync_dynamic_particle_position(system, emitter_id, particle_index);

    if (emitter.affected_by_wind) {
        core.velocity[particle_index] += context.external.wind * dt;
    }
    core.velocity[particle_index].z -= core.mass[particle_index] * dt;

    const glm::vec3 previous_position = core.position[particle_index];
    const auto simulation_space = effective_simulation_space(emitter);
    if ((simulation_space == ParticleSimulationSpace::local
            || simulation_space == ParticleSimulationSpace::emitter_attached
            || simulation_space == ParticleSimulationSpace::spawn_attached)) {
        const auto& emitter_state = system.emitters[emitter_id];
        particles.attachment.local_position[particle_index] +=
            attachment_world_delta_to_local(emitter_state, simulation_space, core.velocity[particle_index] * dt);
        sync_attached_particle_position(system, emitter_id, particle_index);
    } else {
        core.position[particle_index] += core.velocity[particle_index] * dt;
    }

    apply_particle_collision(system, emitter_id, particle_index, previous_position, context);
}

void advance_dynamic_particle(ParticleSystemInstance& system, const CompiledParticleEmitter& emitter,
    const ParticleEmitterState& emitter_state, size_t particle_index, float dt,
    const ParticleSimulationContext& context)
{
    auto& particles = system.particles;
    auto& core = particles.core;

    sync_dynamic_particle_position(system, emitter, emitter_state, particle_index);

    if (emitter.affected_by_wind) {
        core.velocity[particle_index] += context.external.wind * dt;
    }
    core.velocity[particle_index].z -= core.mass[particle_index] * dt;

    const glm::vec3 previous_position = core.position[particle_index];
    const auto simulation_space = effective_simulation_space(emitter);
    if (simulation_space == ParticleSimulationSpace::local
        || simulation_space == ParticleSimulationSpace::emitter_attached
        || simulation_space == ParticleSimulationSpace::spawn_attached) {
        particles.attachment.local_position[particle_index] +=
            attachment_world_delta_to_local(emitter_state, simulation_space, core.velocity[particle_index] * dt);
        sync_attached_particle_position(system, emitter, emitter_state, particle_index);
    } else {
        core.position[particle_index] += core.velocity[particle_index] * dt;
    }

    apply_particle_collision(system, emitter, emitter_state, particle_index, previous_position, context);
}

void build_particle_tick_order(const ParticleCoreStorage& core, std::vector<uint32_t>& order,
    std::vector<uint32_t>& cursor, std::vector<uint32_t>& offsets, size_t emitter_count)
{
    const size_t count = core.age.size();
    order.resize(count);
    offsets.assign(emitter_count + 1, 0u);
    cursor.assign(emitter_count, 0u);

    for (const auto emitter_id : core.emitter_id) {
        if (emitter_id < emitter_count) {
            ++offsets[static_cast<size_t>(emitter_id) + 1];
        }
    }

    for (size_t i = 1; i < offsets.size(); ++i) {
        offsets[i] += offsets[i - 1];
    }

    std::copy(offsets.begin(), offsets.end() - 1, cursor.begin());
    for (uint32_t i = 0; i < count; ++i) {
        const auto emitter_id = core.emitter_id[i];
        if (emitter_id >= emitter_count) { continue; }
        order[cursor[emitter_id]++] = i;
    }
}

float single_shot_repeat_interval(const CompiledParticleEmitter& emitter, const ParticleEmitterState& state)
{
    const float emission_rate = sanitize_emission_rate(eval_keyed_scalar_track(emitter.emission_rate_track, state.time));
    if (emission_rate > 0.0f) {
        return 1.0f / emission_rate;
    }

    const float lifetime = std::max(0.0f, eval_keyed_scalar_track(emitter.lifetime_track, state.time));
    if (lifetime > 0.0f) {
        return lifetime;
    }

    return std::numeric_limits<float>::infinity();
}

struct SpawnedParticleSample {
    float size_x_begin = 0.0f;
    float size_x_end = 0.0f;
    float size_y_begin = 0.0f;
    float size_y_end = 0.0f;
    float lifetime = 0.0f;
    float mass = 0.0f;
    float rotation_rate = 0.0f;
    float spread_radians = 0.0f;
    glm::vec4 color_begin{1.0f};
    glm::vec4 color_end{1.0f};
};

struct ReservedParticleRange {
    size_t begin = 0;
    uint32_t count = 0;
};

SpawnedParticleSample sample_spawned_particle(const CompiledParticleEmitter& emitter, float emitter_time)
{
    const bool keyed_spawn_size = (emitter.features & CompiledParticleFeature::sample_spawn_size) != 0;
    const bool keyed_spawn_color = (emitter.features & CompiledParticleFeature::sample_spawn_color) != 0;
    const bool keyed_spawn_motion = (emitter.features & CompiledParticleFeature::sample_spawn_motion) != 0;

    SpawnedParticleSample result;
    result.size_x_begin = keyed_spawn_size
        ? eval_keyed_scalar_track(emitter.size_start_x_track, emitter_time)
        : emitter.initial.size_x.min;
    result.size_x_end = keyed_spawn_size
        ? eval_keyed_scalar_track(emitter.size_end_x_track, emitter_time)
        : (emitter.over_life.size_x.keys.empty() ? emitter.initial.size_x.min : emitter.over_life.size_x.keys.back().value);
    result.size_y_begin = keyed_spawn_size
        ? eval_keyed_scalar_track(emitter.size_start_y_track, emitter_time)
        : emitter.initial.size_y.min;
    result.size_y_end = keyed_spawn_size
        ? eval_keyed_scalar_track(emitter.size_end_y_track, emitter_time)
        : (emitter.over_life.size_y.keys.empty() ? emitter.initial.size_y.min : emitter.over_life.size_y.keys.back().value);
    result.lifetime = keyed_spawn_motion
        ? std::max(0.0f, eval_keyed_scalar_track(emitter.lifetime_track, emitter_time))
        : std::max(0.0f, emitter.initial.lifetime.min);
    result.mass = keyed_spawn_motion
        ? eval_keyed_scalar_track(emitter.mass_track, emitter_time)
        : emitter.initial.mass;
    result.rotation_rate = keyed_spawn_motion
        ? eval_keyed_scalar_track(emitter.rotation_rate_track, emitter_time)
        : emitter.initial.rotation_rate.min;
    result.spread_radians = keyed_spawn_motion
        ? std::max(0.0f, eval_keyed_scalar_track(emitter.spread_track, emitter_time))
        : emitter.initial.spread_radians;
    result.color_begin = keyed_spawn_color
        ? eval_keyed_color_track(emitter.color_start_track, emitter_time)
        : emitter.initial.color;
    result.color_end = keyed_spawn_color
        ? eval_keyed_color_track(emitter.color_end_track, emitter_time)
        : (emitter.over_life.color.keys.empty() ? emitter.initial.color : emitter.over_life.color.keys.back().value);
    result.color_begin.a = keyed_spawn_color
        ? eval_keyed_scalar_track(emitter.alpha_start_track, emitter_time)
        : emitter.initial.color.a;
    result.color_end.a = keyed_spawn_color
        ? eval_keyed_scalar_track(emitter.alpha_end_track, emitter_time)
        : (emitter.over_life.alpha.keys.empty() ? emitter.initial.color.a : emitter.over_life.alpha.keys.back().value);
    return result;
}

ReservedParticleRange reserve_spawn_range(ParticleSystemInstance& system, uint16_t emitter_id, uint32_t requested_count)
{
    auto& core = system.particles.core;
    const auto& emitter = system.effect->emitters[emitter_id];
    auto& live_particles = system.live_particles_per_emitter;
    if (core.age.size() >= system.effect->max_particles_total || live_particles[emitter_id] >= emitter.max_particles) {
        return {.begin = core.age.size(), .count = 0};
    }
    const uint32_t available_total =
        static_cast<uint32_t>(std::min<size_t>(std::numeric_limits<uint32_t>::max(), system.effect->max_particles_total - core.age.size()));
    const uint32_t available_emitter = emitter.max_particles - live_particles[emitter_id];
    const uint32_t spawn_count = std::min(requested_count, std::min(available_total, available_emitter));
    if (spawn_count == 0) {
        return {.begin = core.age.size(), .count = 0};
    }

    const size_t begin = core.age.size();
    const size_t end = begin + spawn_count;
    resize_core_storage(core, end);
    resize_particle_sidecars(system.particles, end);
    live_particles[emitter_id] += spawn_count;
    return {.begin = begin, .count = spawn_count};
}

void spawn_particle_at(ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index, const glm::vec3& base_position)
{
    auto& particles = system.particles;
    auto& core = particles.core;
    const auto& emitter = system.effect->emitters[emitter_id];
    const auto* material = get_material(*system.effect, emitter.material);
    auto& state = system.emitters[emitter_id];
    const SpawnedParticleSample sample = sample_spawned_particle(emitter, state.time);
    SpawnedParticleSample tuned_sample = sample;

    if (uses_projectile_spark_trail(emitter, material)) {
        tuned_sample.lifetime = std::min(tuned_sample.lifetime, 0.35f);
    }

    const glm::vec3 world_position = spawn_position(state, emitter, base_position);
    const glm::vec3 emission_direction = initial_emission_direction(emitter, state, tuned_sample.spread_radians);
    const glm::vec3 velocity = initial_velocity(emitter, state, emission_direction);
    const glm::vec3 render_direction = initial_render_direction(emitter, state, velocity, emission_direction);
    const auto& sheet = state.sampled_sheet;
    const uint16_t frame_offset = initial_frame_offset(sheet, state);

    core.position[particle_index] = world_position;
    core.velocity[particle_index] = velocity;
    core.render_direction[particle_index] = render_direction;
    core.age[particle_index] = 0.0f;
    core.lifetime[particle_index] = tuned_sample.lifetime;
    core.mass[particle_index] = tuned_sample.mass;
    core.size_x[particle_index] = tuned_sample.size_x_begin;
    core.size_y[particle_index] = tuned_sample.size_y_begin;
    core.size_x_begin[particle_index] = tuned_sample.size_x_begin;
    core.size_x_end[particle_index] = tuned_sample.size_x_end;
    core.size_y_begin[particle_index] = tuned_sample.size_y_begin;
    core.size_y_end[particle_index] = tuned_sample.size_y_end;
    core.rotation[particle_index] = emitter.initial.rotation.min;
    core.rotation_rate[particle_index] = sample.rotation_rate;
    core.frame[particle_index] = eval_sprite_frame(sheet, 0.0f, 0.0f, frame_offset);
    core.frame_offset[particle_index] = frame_offset;
    core.color_rgba8[particle_index] = pack_color(tuned_sample.color_begin);
    core.color_begin[particle_index] = tuned_sample.color_begin;
    core.color_end[particle_index] = tuned_sample.color_end;
    core.normalized_age[particle_index] = 0.0f;
    core.emitter_id[particle_index] = emitter_id;
    core.material_id[particle_index] = static_cast<uint16_t>(emitter.material);
    set_attachment_state(particles, particle_index, emitter, state, base_position, world_position);
    set_bezier_state(particles, particle_index, emitter, state, world_position);
    set_beam_state(particles, particle_index, emitter, state, world_position);
}

void spawn_particle(ParticleSystemInstance& system, uint16_t emitter_id, const glm::vec3& base_position)
{
    NW_PROFILE_SCOPE_N("spawn_particle");
    NW_PROFILE_VALUE(emitter_id);
    const auto range = reserve_spawn_range(system, emitter_id, 1);
    if (range.count == 0) { return; }
    spawn_particle_at(system, emitter_id, range.begin, base_position);
}

size_t find_particle_for_emitter(const ParticleCoreStorage& core, uint16_t emitter_id)
{
    for (size_t i = 0; i < core.emitter_id.size(); ++i) {
        if (core.emitter_id[i] == emitter_id) { return i; }
    }
    return static_cast<size_t>(-1);
}

void ensure_beam_particle(ParticleSystemInstance& system, uint16_t emitter_id)
{
    NW_PROFILE_SCOPE_N("ensure_beam_particle");
    if (find_particle_for_emitter(system.particles.core, emitter_id) != static_cast<size_t>(-1)) { return; }
    spawn_particle(system, emitter_id, emitter_position(system.emitters[emitter_id]));
    if (!system.particles.core.lifetime.empty()) {
        // Beam particles persist until the emitter is disabled or retargeted.
        system.particles.core.lifetime.back() = kBeamPerpetualLifetime;
    }
}

void spawn_continuous_particles(ParticleSystemInstance& system, uint16_t emitter_id, float dt)
{
    NW_PROFILE_SCOPE_N("spawn_continuous_particles");
    NW_PROFILE_VALUE(emitter_id);
    const auto& emitter = system.effect->emitters[emitter_id];
    auto& state = system.emitters[emitter_id];
    const glm::vec3 current_pos = emitter_position(state);
    const glm::vec3 previous_pos = state.prev_world_pos;
    const auto simulation_space = effective_simulation_space(emitter);
    const bool emit_in_current_emitter_space = (simulation_space == ParticleSimulationSpace::local
        || simulation_space == ParticleSimulationSpace::emitter_attached);
    const float emission_rate = sanitize_emission_rate(eval_keyed_scalar_track(emitter.emission_rate_track, state.time));

    if (emitter.emission.metric == ParticleSpawnMetric::per_second) {
        state.spawn_accumulator += emission_rate * dt;
        const auto requested_count = static_cast<uint32_t>(state.spawn_accumulator);
        state.spawn_accumulator -= requested_count;
        const auto range = reserve_spawn_range(system, emitter_id, requested_count);

        for (uint32_t n = 0; n < range.count; ++n) {
            const float t = (static_cast<float>(n) + 1.0f) / (static_cast<float>(range.count) + 1.0f);
            const glm::vec3 base_position = emit_in_current_emitter_space
                ? current_pos
                : glm::mix(previous_pos, current_pos, t);
            spawn_particle_at(system, emitter_id, range.begin + n, base_position);
        }
        return;
    }

    if (emitter.emission.metric == ParticleSpawnMetric::per_distance && emission_rate > 0.0f) {
        const float distance = glm::distance(previous_pos, current_pos);
        state.distance_accumulator += distance * emission_rate;
        const auto requested_count = static_cast<uint32_t>(state.distance_accumulator);
        state.distance_accumulator -= requested_count;
        const auto range = reserve_spawn_range(system, emitter_id, requested_count);

        for (uint32_t n = 0; n < range.count; ++n) {
            const float t = (static_cast<float>(n) + 1.0f) / (static_cast<float>(range.count) + 1.0f);
            const glm::vec3 base_position = emit_in_current_emitter_space
                ? current_pos
                : glm::mix(previous_pos, current_pos, t);
            spawn_particle_at(system, emitter_id, range.begin + n, base_position);
        }
    }
}

uint32_t render_sort_group_key(const CompiledParticleEffect& effect, const ParticleCoreStorage& core, size_t particle_index)
{
    const auto emitter_id = core.emitter_id[particle_index];
    const auto& emitter = effect.emitters[emitter_id];
    const uint32_t sort_order = static_cast<uint32_t>(effect.emitters[emitter_id].render.sort_order);
    const bool preserve_emitter_partition = emitter.kernel == CompiledParticleKernel::mesh_basic
        || emitter.render.mode == ParticleRenderMode::linked_chain
        || emitter.render.mode == ParticleRenderMode::beam;
    const uint32_t emitter_group = preserve_emitter_partition
        ? static_cast<uint32_t>(emitter_id)
        : 0u;
    return (sort_order << 24)
        | (static_cast<uint32_t>(core.material_id[particle_index]) << 8)
        | emitter_group;
}

uint64_t render_sort_key(const CompiledParticleEffect& effect, const ParticleCoreStorage& core, size_t particle_index)
{
    const uint64_t group_key = static_cast<uint64_t>(render_sort_group_key(effect, core, particle_index)) << 32;
    const auto emitter_id = core.emitter_id[particle_index];
    if (effect.emitters[emitter_id].render.mode != ParticleRenderMode::linked_chain) {
        return group_key;
    }

    // Ages are non-negative, so IEEE-754 bit order is monotonic. Invert for descending age ordering.
    const uint32_t chain_key = std::numeric_limits<uint32_t>::max() - std::bit_cast<uint32_t>(core.age[particle_index]);
    return group_key | chain_key;
}

void sort_particles_for_render(const CompiledParticleEffect& effect, const ParticleStorage& particles,
    std::vector<uint32_t>& order, std::vector<uint64_t>& keys)
{
    NW_PROFILE_SCOPE_N("sort_particles_for_render");
    const auto& core = particles.core;
    const size_t count = core.age.size();

    order.resize(count);
    keys.resize(count);
    std::iota(order.begin(), order.end(), uint32_t{0});
    for (size_t i = 0; i < count; ++i) {
        keys[i] = render_sort_key(effect, core, i);
    }

    std::stable_sort(order.begin(), order.end(), [&](uint32_t lhs, uint32_t rhs) {
        return keys[lhs] < keys[rhs];
    });
}

ParticleRenderPath build_render_path(const ParticleSystemInstance& system, uint16_t emitter_id, size_t particle_index)
{
    ParticleRenderPath path{};
    const auto& emitter = system.effect->emitters[emitter_id];

    switch (emitter.kernel) {
    case CompiledParticleKernel::beam_lightning:
        path.kind = ParticleRenderPathKind::beam;
        path.source = system.particles.beams.source_position[particle_index];
        path.target = system.particles.beams.target_position[particle_index];
        path.subdivisions = system.particles.beams.subdivisions[particle_index];
        break;
    case CompiledParticleKernel::sprite_target_bezier:
        // These particles travel independently and render as billboards.
        break;
    case CompiledParticleKernel::sprite_target_gravity:
        path.source = emitter_position(system.emitters[emitter_id]);
        path.target = system.emitters[emitter_id].target_point;
        break;
    case CompiledParticleKernel::linked_chain:
        switch (emitter.targeting.mode) {
        case ParticleTargetingMode::point_bezier: {
            path.kind = ParticleRenderPathKind::bezier;
            path.source = system.particles.bezier.source_position[particle_index];
            path.target = system.emitters[emitter_id].target_point;
            const glm::vec3 target_dir = bezier_target_direction(path.source, path.target);
            path.control1 = path.source + target_dir * emitter.targeting.source_tangent;
            path.control2 = path.target - target_dir * emitter.targeting.target_tangent;
            break;
        }
        case ParticleTargetingMode::point_gravity:
            path.source = emitter_position(system.emitters[emitter_id]);
            path.target = system.emitters[emitter_id].target_point;
            break;
        case ParticleTargetingMode::none:
        case ParticleTargetingMode::beam_lightning:
            break;
        }
        break;
    case CompiledParticleKernel::mesh_basic:
    case CompiledParticleKernel::sprite_basic_constant:
    case CompiledParticleKernel::sprite_basic:
        break;
    }

    return path;
}

bool same_particle_sheet(const ParticleSpriteSheet& lhs, const ParticleSpriteSheet& rhs)
{
    return lhs.columns == rhs.columns
        && lhs.rows == rhs.rows
        && lhs.frame_begin == rhs.frame_begin
        && lhs.frame_end == rhs.frame_end
        && lhs.frames_per_second == rhs.frames_per_second
        && lhs.random_start == rhs.random_start;
}

bool supports_cross_emitter_batching(const CompiledParticleEmitter& emitter)
{
    return emitter.kernel != CompiledParticleKernel::mesh_basic
        && emitter.render.mode != ParticleRenderMode::linked_chain
        && emitter.render.mode != ParticleRenderMode::beam;
}

bool compatible_packet_state(const ParticleSystemInstance& system, uint32_t material_id, uint16_t lhs_emitter_id, uint16_t rhs_emitter_id)
{
    if (!system.effect) { return false; }
    if (lhs_emitter_id >= system.effect->emitters.size() || rhs_emitter_id >= system.effect->emitters.size()) {
        return false;
    }
    if (material_id >= system.effect->materials.size()) {
        return false;
    }

    const auto& lhs = system.effect->emitters[lhs_emitter_id];
    const auto& rhs = system.effect->emitters[rhs_emitter_id];
    if (!supports_cross_emitter_batching(lhs) || !supports_cross_emitter_batching(rhs)) {
        return lhs_emitter_id == rhs_emitter_id;
    }

    const auto packet_mode = [](const CompiledParticleEmitter& emitter) {
        return emitter.kernel == CompiledParticleKernel::mesh_basic
            ? ParticleRenderMode::mesh
            : emitter.render.mode;
    };

    return packet_mode(lhs) == packet_mode(rhs)
        && lhs.render.sort_order == rhs.render.sort_order
        && lhs.render.semantic == rhs.render.semantic
        && lhs.render.tint_to_scene_ambient == rhs.render.tint_to_scene_ambient
        && lhs.render.opacity_scale == rhs.render.opacity_scale
        && lhs.render.blur_length == rhs.render.blur_length
        && lhs.render.deadspace_radians == rhs.render.deadspace_radians
        && lhs.region.type == rhs.region.type
        && (lhs.region.type != ParticleSpawnRegionType::rect
                || (lhs.region.size.x == rhs.region.size.x && lhs.region.size.y == rhs.region.size.y))
        && same_particle_sheet(system.emitters[lhs_emitter_id].sampled_sheet, system.emitters[rhs_emitter_id].sampled_sheet);
}

} // namespace

ParticleSystemInstance create_particle_system(const CompiledParticleEffect& effect)
{
    NW_PROFILE_SCOPE();
    NW_PROFILE_PLOT("particles/create/max_particles_total", static_cast<double>(effect.max_particles_total));
    ParticleSystemInstance result;
    result.effect = &effect;
    result.particles.has_bezier = (effect.storage & CompiledParticleStorage::bezier) != 0;
    result.particles.has_beams = (effect.storage & CompiledParticleStorage::beams) != 0;
    result.particles.has_attachment = (effect.storage & CompiledParticleStorage::attachment) != 0;
    result.emitters.resize(effect.emitters.size());
    result.live_particles_per_emitter.resize(effect.emitters.size());
    result.emitter_visible.resize(effect.emitters.size(), 1u);
    for (size_t i = 0; i < result.emitters.size(); ++i) {
        auto& emitter = result.emitters[i];
        emitter.prev_world_pos = emitter_position(emitter);
        emitter.inverse_world_transform = glm::mat4{1.0f};
        emitter.random_seed = 0x12345678u + static_cast<uint32_t>(i) * 0x9e3779b9u;
        emitter.sampled_sheet = effect.emitters[i].material < effect.materials.size()
            ? effect.materials[effect.emitters[i].material].sheet
            : ParticleSpriteSheet{};
    }

    const size_t reserve = effect.max_particles_total;
    reserve_core_storage(result.particles.core, reserve);
    reserve_particle_sidecars(result.particles, reserve);
    result.render_packets.packets.reserve(effect.emitters.size());
    result.force_events.reserve(effect.emitters.size());
    result.tick_offsets.reserve(effect.emitters.size() + 1);
    result.sort_order.reserve(reserve);
    result.sort_target.reserve(reserve);
    result.sort_keys.reserve(reserve);

    return result;
}

void apply_particle_attachment_defaults(ParticleSystemInstance& system)
{
    if (!system.effect) { return; }

    const size_t count = std::min(system.effect->emitters.size(), system.emitters.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& attachment = system.effect->emitters[i].attachment;
        auto& emitter = system.emitters[i];
        if (attachment.has_default_transform) {
            emitter.world_transform = attachment.default_transform;
            emitter.prev_world_pos = glm::vec3(attachment.default_transform[3]);
        } else if (attachment.has_default_position) {
            emitter.world_transform = glm::translate(glm::mat4{1.0f}, attachment.default_position);
            emitter.prev_world_pos = attachment.default_position;
            if (attachment.has_default_orientation) {
                emitter.world_transform *= glm::mat4_cast(attachment.default_orientation);
            }
        } else if (attachment.has_default_orientation) {
            emitter.world_transform = glm::mat4_cast(attachment.default_orientation);
        }
        if (attachment.has_default_target_offset) {
            emitter.target_point = attachment.default_target_offset;
        }
    }
}

void kill_particle(ParticleSystemInstance& system, size_t particle_index)
{
    auto& particles = system.particles;
    auto& core = particles.core;
    if (particle_index >= core.age.size()) { return; }

    const auto emitter_id = core.emitter_id[particle_index];
    if (emitter_id < system.live_particles_per_emitter.size() && system.live_particles_per_emitter[emitter_id] > 0) {
        --system.live_particles_per_emitter[emitter_id];
    }

    const size_t live = core.age.size() - 1;
    swap_particle(particles, particle_index, live);
    resize_core_storage(core, live);
    resize_particle_sidecars(particles, live);
}

void trigger_particle_emitter(ParticleSystemInstance& system, uint16_t emitter_id, uint32_t burst_count)
{
    NW_PROFILE_SCOPE();
    NW_PROFILE_VALUE(emitter_id);
    NW_PROFILE_VALUE(burst_count);

    if (!system.effect || emitter_id >= system.emitters.size() || burst_count == 0) { return; }

    const auto& emitter_def = system.effect->emitters[emitter_id];
    auto& emitter = system.emitters[emitter_id];
    if (emitter_def.emission.trigger_on_effect_events
        && emitter_def.emission.mode == ParticleEmissionMode::continuous) {
        if (is_projectile_body_sprite(emitter_def)) {
            emitter.pulse_time_remaining = std::numeric_limits<float>::infinity();
        } else {
            emitter.pulse_time_remaining += emitter_def.emission.effect_event_period * static_cast<float>(burst_count);
        }
        emitter.active = true;
        return;
    }

    emitter.pending_bursts += burst_count;
    emitter.active = true;
}

void spawn_pending_bursts(ParticleSystemInstance& system, uint16_t emitter_id, const glm::vec3& current_pos)
{
    const auto& emitter = system.effect->emitters[emitter_id];
    auto& state = system.emitters[emitter_id];
    if (state.pending_bursts == 0) { return; }

    const float emission_rate = sanitize_emission_rate(eval_keyed_scalar_track(emitter.emission_rate_track, state.time));
    const float particles_per_burst_f = emitter.emission.trigger_on_effect_events
        ? emission_rate * std::max(emitter.emission.effect_event_period, 0.0f)
        : emission_rate;
    const uint32_t particles_per_burst =
        static_cast<uint32_t>(std::lround(std::max(0.0f, particles_per_burst_f)));
    const auto range = reserve_spawn_range(system, emitter_id, state.pending_bursts * particles_per_burst);
    size_t write = range.begin;
    for (uint32_t burst = 0; burst < state.pending_bursts; ++burst) {
        if (emitter.force_event.radius > 0.0f || emitter.force_event.length > 0.0f) {
            system.force_events.push_back(
                {emitter_id, current_pos, emitter.force_event.radius, emitter.force_event.length});
        }
        for (uint32_t n = 0; n < particles_per_burst; ++n) {
            if (write >= range.begin + range.count) { break; }
            spawn_particle_at(system, emitter_id, write, current_pos);
            ++write;
        }
    }
    state.pending_bursts = 0;
}

void tick_particle_system(ParticleSystemInstance& system, float dt, const ParticleExternalForces& forces)
{
    tick_particle_system(system, dt, ParticleSimulationContext{.external = forces});
}

void tick_particle_system(ParticleSystemInstance& system, float dt, const ParticleSimulationContext& context)
{
    NW_PROFILE_SCOPE();
    if (!system.effect || dt <= 0.0f) { return; }

    system.force_events.clear();
    auto& particles = system.particles;
    auto& core = particles.core;
    NW_PROFILE_PLOT("particles/tick/live_before", static_cast<double>(core.age.size()));
    NW_PROFILE_PLOT("particles/tick/dt", static_cast<double>(dt));

    {
        NW_PROFILE_SCOPE_N("tick_emitters");
        for (size_t i = 0; i < system.effect->emitters.size(); ++i) {
            const auto emitter_id = static_cast<uint16_t>(i);
            const auto& emitter = system.effect->emitters[emitter_id];
            auto& state = system.emitters[emitter_id];
            if (!state.active) { continue; }
            const glm::vec3 current_pos = emitter_position(state);
            state.frame_delta = current_pos - state.prev_world_pos;
            state.linear_velocity = dt > 0.0f ? state.frame_delta / dt : glm::vec3{0.0f};
            state.inverse_world_transform = glm::inverse(state.world_transform);
            state.time += dt;
            const auto* material = get_material(*system.effect, emitter.material);
            const ParticleSpriteSheet base_sheet = material ? material->sheet : ParticleSpriteSheet{};
            state.sampled_sheet = (material
                                      && (emitter.features & (CompiledParticleFeature::sample_spawn_frame | CompiledParticleFeature::update_frame)) != 0)
                ? eval_emitter_sheet(emitter, *material, state.time)
                : base_sheet;

            switch (emitter.emission.mode) {
            case ParticleEmissionMode::continuous:
                if (emitter.emission.trigger_on_effect_events) {
                    const float emit_dt = std::min(dt, state.pulse_time_remaining);
                    if (emit_dt > 0.0f) {
                        spawn_continuous_particles(system, emitter_id, emit_dt);
                        state.pulse_time_remaining = std::max(0.0f, state.pulse_time_remaining - dt);
                    }
                } else {
                    spawn_continuous_particles(system, emitter_id, dt);
                }
                break;
            case ParticleEmissionMode::single_shot:
                state.single_shot_cooldown -= dt;
                if (state.single_shot_cooldown <= 0.0f) {
                    spawn_particle(system, emitter_id, current_pos);
                    if (emitter.emission.looping) {
                        state.single_shot_cooldown += single_shot_repeat_interval(emitter, state);
                    } else {
                        state.active = false;
                        state.single_shot_cooldown = 0.0f;
                    }
                }
                break;
            case ParticleEmissionMode::event_burst:
                spawn_pending_bursts(system, emitter_id, current_pos);
                break;
            case ParticleEmissionMode::beam_continuous:
                ensure_beam_particle(system, emitter_id);
                break;
            }

            state.prev_world_pos = current_pos;
        }
    }

    {
        NW_PROFILE_SCOPE_N("tick_particles");
        build_particle_tick_order(core, system.sort_order, system.sort_target, system.tick_offsets, system.emitters.size());

        for (size_t emitter_index = 0; emitter_index < system.effect->emitters.size(); ++emitter_index) {
            const uint32_t begin = system.tick_offsets[emitter_index];
            const uint32_t end = system.tick_offsets[emitter_index + 1];
            if (begin == end) { continue; }

            const auto emitter_id = static_cast<uint16_t>(emitter_index);
            const auto& emitter = system.effect->emitters[emitter_index];
            const auto& emitter_state = system.emitters[emitter_index];

            switch (emitter.kernel) {
            case CompiledParticleKernel::sprite_target_gravity:
                for (uint32_t order_index = begin; order_index < end; ++order_index) {
                    const size_t i = system.sort_order[order_index];
                    core.age[i] += dt;
                    const float lifetime = std::max(core.lifetime[i], 1.0e-6f);
                    const float normalized_age = std::clamp(core.age[i] / lifetime, 0.0f, 1.0f);
                    core.normalized_age[i] = normalized_age;

                    sync_dynamic_particle_position(system, emitter, emitter_state, i);
                    const glm::vec3 delta = emitter_state.target_point - core.position[i];
                    const float distance = glm::length(delta);
                    if (emitter.targeting.kill_radius > 0.0f && distance <= emitter.targeting.kill_radius) {
                        core.age[i] = core.lifetime[i];
                        continue;
                    }

                    if (distance > 1.0e-6f) {
                        const glm::vec3 direction = delta / distance;
                        core.velocity[i] += direction * emitter.targeting.gravity * dt;
                        if (emitter.targeting.drag != 0.0f) {
                            const float drag_scale = std::max(0.0f, 1.0f - emitter.targeting.drag * dt);
                            core.velocity[i] *= drag_scale;
                        }
                    }

                    advance_dynamic_particle(system, emitter, emitter_state, i, dt, context);
                    update_particle_presentation(system, emitter, emitter_state, i);
                }
                break;
            case CompiledParticleKernel::sprite_target_bezier:
                for (uint32_t order_index = begin; order_index < end; ++order_index) {
                    const size_t i = system.sort_order[order_index];
                    core.age[i] += dt;
                    const float lifetime = std::max(core.lifetime[i], 1.0e-6f);
                    const float normalized_age = std::clamp(core.age[i] / lifetime, 0.0f, 1.0f);
                    core.normalized_age[i] = normalized_age;

                    advance_attached_bezier_path(system, emitter, emitter_state, i);
                    const float path_t = eval_path_progress(normalized_age, core.age[i], emitter.targeting.transition_factor);
                    core.position[i] = cubic_bezier(
                        particles.bezier.source_position[i],
                        particles.bezier.source_tangent[i],
                        particles.bezier.target_tangent[i],
                        particles.bezier.target_position[i],
                        path_t);
                    update_particle_presentation(system, emitter, emitter_state, i);
                }
                break;
            case CompiledParticleKernel::beam_lightning:
                for (uint32_t order_index = begin; order_index < end; ++order_index) {
                    const size_t i = system.sort_order[order_index];
                    core.age[i] += dt;
                    const float lifetime = std::max(core.lifetime[i], 1.0e-6f);
                    const float normalized_age = std::clamp(core.age[i] / lifetime, 0.0f, 1.0f);
                    core.normalized_age[i] = normalized_age;

                    particles.beams.source_position[i] = emitter_position(emitter_state);
                    particles.beams.target_position[i] = emitter_state.target_point;
                    particles.beams.update_accumulator[i] += dt;
                    if (emitter.beam.update_interval > 0.0f
                        && particles.beams.update_accumulator[i] >= emitter.beam.update_interval) {
                        particles.beams.update_accumulator[i] = 0.0f;
                    }
                    core.position[i] = 0.5f * (particles.beams.source_position[i] + particles.beams.target_position[i]);
                    update_particle_presentation(system, emitter, emitter_state, i);
                }
                break;
            case CompiledParticleKernel::linked_chain:
                if (emitter.targeting.mode == ParticleTargetingMode::point_bezier) {
                    for (uint32_t order_index = begin; order_index < end; ++order_index) {
                        const size_t i = system.sort_order[order_index];
                        core.age[i] += dt;
                        const float lifetime = std::max(core.lifetime[i], 1.0e-6f);
                        const float normalized_age = std::clamp(core.age[i] / lifetime, 0.0f, 1.0f);
                        core.normalized_age[i] = normalized_age;

                        const float path_t = eval_path_progress(normalized_age, core.age[i], emitter.targeting.transition_factor);
                        core.position[i] = cubic_bezier(
                            particles.bezier.source_position[i],
                            particles.bezier.source_tangent[i],
                            particles.bezier.target_tangent[i],
                            particles.bezier.target_position[i],
                            path_t);
                        update_particle_presentation(system, emitter, emitter_state, i);
                    }
                    break;
                }
                [[fallthrough]];
            case CompiledParticleKernel::mesh_basic:
            case CompiledParticleKernel::sprite_basic_constant:
            case CompiledParticleKernel::sprite_basic:
                if (BasicParticleSimdUpdate simd_update;
                    make_basic_particle_simd_update(emitter, context, simd_update)) {
                    size_t first = 0;
                    if (particle_order_is_contiguous(system.sort_order, begin, end, first)) {
                        update_basic_particles_simd(core, simd_update, first, end - begin, dt);
                        break;
                    }
                }

                for (uint32_t order_index = begin; order_index < end; ++order_index) {
                    const size_t i = system.sort_order[order_index];
                    core.age[i] += dt;
                    const float lifetime = std::max(core.lifetime[i], 1.0e-6f);
                    const float normalized_age = std::clamp(core.age[i] / lifetime, 0.0f, 1.0f);
                    core.normalized_age[i] = normalized_age;

                    advance_dynamic_particle(system, emitter, emitter_state, i, dt, context);
                    update_particle_presentation(system, emitter, emitter_state, i);
                }
                break;
            }
        }
    }

    remove_dead_particles(particles, system.live_particles_per_emitter);
    NW_PROFILE_PLOT("particles/tick/live_after", static_cast<double>(particles.core.age.size()));
}

std::span<const ParticleRenderPacket> build_particle_render_packets(ParticleSystemInstance& system)
{
    NW_PROFILE_SCOPE();
    auto& packets = system.render_packets.packets;
    packets.clear();

    if (!system.effect) { return {}; }

    auto& core = system.particles.core;
    if (core.age.empty()) { return system.render_packets.span(); }

    sort_particles_for_render(*system.effect, system.particles, system.sort_order, system.sort_keys);

    size_t begin = 0;
    while (begin < system.sort_order.size()) {
        const size_t first_particle = system.sort_order[begin];
        if (first_particle >= core.age.size()) {
            ++begin;
            continue;
        }

        const auto material = core.material_id[first_particle];
        const auto emitter_id = core.emitter_id[first_particle];
        size_t end = begin + 1;

        while (end < system.sort_order.size()) {
            const size_t particle_index = system.sort_order[end];
            if (particle_index >= core.age.size()
                || core.material_id[particle_index] != material
                || !compatible_packet_state(system, material, emitter_id, core.emitter_id[particle_index])) {
                break;
            }
            ++end;
        }

        if (emitter_id >= system.emitter_visible.size() || system.emitter_visible[emitter_id] == 0u) {
            begin = end;
            continue;
        }

        const auto& emitter = system.effect->emitters[emitter_id];
        const auto blend = material < system.effect->materials.size()
            ? system.effect->materials[material].blend
            : ParticleBlendMode::alpha;
        const bool double_sided = material < system.effect->materials.size()
            ? system.effect->materials[material].double_sided
            : false;
        const ParticleSpriteSheet packet_sheet = system.emitters[emitter_id].sampled_sheet;
        bool uniform_color = (emitter.features & CompiledParticleFeature::update_color) == 0;
        bool uniform_frame = (emitter.features & CompiledParticleFeature::update_frame) == 0;
        bool uniform_rotation = (emitter.features & CompiledParticleFeature::update_rotation) == 0;
        for (size_t i = begin + 1; i < end; ++i) {
            const size_t particle_index = system.sort_order[i];
            if (particle_index >= core.age.size()) {
                continue;
            }
            const auto packet_emitter_id = core.emitter_id[particle_index];
            if (packet_emitter_id >= system.effect->emitters.size()) {
                continue;
            }
            const auto& packet_emitter = system.effect->emitters[packet_emitter_id];
            uniform_color = uniform_color
                && (packet_emitter.features & CompiledParticleFeature::update_color) == 0;
            uniform_frame = uniform_frame
                && (packet_emitter.features & CompiledParticleFeature::update_frame) == 0;
            uniform_rotation = uniform_rotation
                && (packet_emitter.features & CompiledParticleFeature::update_rotation) == 0;
        }

        packets.push_back(ParticleRenderPacket{
            .mode = emitter.kernel == CompiledParticleKernel::mesh_basic
                ? ParticleRenderMode::mesh
                : emitter.render.mode,
            .semantic = emitter.render.semantic,
            .material = material,
            .begin = static_cast<uint32_t>(begin),
            .count = static_cast<uint32_t>(end - begin),
            .blend = blend,
            .sheet = packet_sheet,
            .path = build_render_path(system, emitter_id, first_particle),
            .double_sided = double_sided,
            .transparent = blend != ParticleBlendMode::cutout,
            .sort_back_to_front = blend == ParticleBlendMode::alpha && emitter.render.mode != ParticleRenderMode::linked_chain,
            .preserve_sequence = emitter.render.mode == ParticleRenderMode::linked_chain,
            .uniform_color = uniform_color,
            .uniform_frame = uniform_frame,
            .uniform_rotation = uniform_rotation,
            .tint_to_scene_ambient = emitter.render.tint_to_scene_ambient,
            .opacity_scale = emitter.render.opacity_scale,
            .blur_length = emitter.render.blur_length,
            .deadspace_radians = emitter.render.deadspace_radians,
            .max_half_width = emitter.region.type == ParticleSpawnRegionType::rect
                ? 0.5f * 0.01f * std::min(emitter.region.size.x, emitter.region.size.y)
                : 0.0f,
        });

        begin = end;
    }

    NW_PROFILE_PLOT("particles/render_packets/count", static_cast<double>(packets.size()));
    return system.render_packets.span();
}

std::span<const uint32_t> particle_render_packet_indices(
    const ParticleSystemInstance& system, const ParticleRenderPacket& packet) noexcept
{
    const size_t begin = packet.begin;
    if (begin >= system.sort_order.size()) { return {}; }

    const size_t available = system.sort_order.size() - begin;
    const size_t count = std::min<size_t>(packet.count, available);
    return {system.sort_order.data() + begin, count};
}

std::span<const ParticleForceEvent> get_particle_force_events(const ParticleSystemInstance& system)
{
    return system.force_events;
}

} // namespace nw::render
