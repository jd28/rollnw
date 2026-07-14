#pragma once

#include "model_attachment.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace nw::render {

enum class ParticleEmissionMode {
    continuous,
    single_shot,
    event_burst,
    beam_continuous,
};

enum class ParticleSpawnMetric {
    per_second,
    per_distance,
};

enum class ParticleSpawnRegionType {
    point,
    rect,
};

enum class ParticleSimulationSpace {
    world,
    local,
    emitter_attached,
    spawn_attached,
};

enum class ParticleRenderMode {
    billboard,
    billboard_local_z,
    billboard_world_z,
    aligned_world_z,
    velocity_aligned,
    stretched,
    linked_chain,
    beam,
    mesh,
};

enum class ParticleRenderSemantic {
    none,
    projectile_body_sprite,
};

enum class ParticleTargetingMode {
    none,
    point_gravity,
    point_bezier,
    beam_lightning,
};

enum class ParticleBlendMode {
    alpha,
    cutout,
    additive,
};

struct ParticleRangeF32 {
    float min = 0.0f;
    float max = 0.0f;
};

struct ParticleCurveKeyF32 {
    float time = 0.0f;
    float value = 0.0f;
};

struct ParticleCurveF32 {
    std::vector<ParticleCurveKeyF32> keys;
};

struct ParticleGradientKey {
    float time = 0.0f;
    glm::vec4 value{1.0f};
};

struct ParticleGradient {
    std::vector<ParticleGradientKey> keys;
};

struct ParticleSpawnRegion {
    ParticleSpawnRegionType type = ParticleSpawnRegionType::point;
    glm::vec2 size{0.0f};
};

struct ParticleSpriteSheet {
    uint16_t columns = 1;
    uint16_t rows = 1;
    uint16_t frame_begin = 0;
    uint16_t frame_end = 0;
    float frames_per_second = 0.0f;
    bool random_start = false;
};

struct ParticleMaterialDef {
    std::string name;
    std::string texture;
    std::string mesh;
    ParticleBlendMode blend = ParticleBlendMode::alpha;
    bool double_sided = false;
    ParticleSpriteSheet sheet;
};

struct ParticleInitialStateDef {
    ParticleRangeF32 lifetime;
    ParticleRangeF32 speed;
    ParticleRangeF32 rotation;
    ParticleRangeF32 rotation_rate;
    ParticleRangeF32 size_x;
    ParticleRangeF32 size_y;
    float spread_radians = 0.0f;
    float mass = 0.0f;
    float velocity_inheritance = 0.0f;
    glm::vec4 color{1.0f};
};

struct ParticleOverLifeDef {
    ParticleCurveF32 alpha;
    ParticleCurveF32 size_x;
    ParticleCurveF32 size_y;
    ParticleCurveF32 rotation;
    ParticleGradient color;
};

struct ParticleTargetingDef {
    ParticleTargetingMode mode = ParticleTargetingMode::none;
    float transition_factor = 0.0f;
    float gravity = 0.0f;
    float drag = 0.0f;
    float kill_radius = 0.0f;
    float source_tangent = 0.0f;
    float target_tangent = 0.0f;
};

struct ParticleBeamDef {
    float update_interval = 0.0f;
    float jitter_radius = 0.0f;
    float noise_scale = 0.0f;
    uint16_t subdivisions = 0;
};

struct ParticleForceEventDef {
    float radius = 0.0f;
    float length = 0.0f;
};

struct ParticleCollisionDef {
    bool enabled = false;
    bool bounce = false;
    bool splat = false;
    float bounce_coefficient = 0.0f;
};

struct ParticleSpawnOverTimeDef {
    ParticleCurveF32 alpha_start;
    ParticleCurveF32 alpha_end;
    ParticleCurveF32 lifetime;
    ParticleCurveF32 speed;
    ParticleCurveF32 speed_random;
    ParticleCurveF32 mass;
    ParticleCurveF32 rotation_rate;
    ParticleCurveF32 spread;
    ParticleCurveF32 sheet_frame_begin;
    ParticleCurveF32 sheet_frame_end;
    ParticleCurveF32 sheet_fps;
    ParticleCurveF32 sheet_random_start;
    ParticleCurveF32 size_start_x;
    ParticleCurveF32 size_end_x;
    ParticleCurveF32 size_start_y;
    ParticleCurveF32 size_end_y;
    ParticleGradient color_start;
    ParticleGradient color_end;
};

struct ParticleRenderDef {
    ParticleRenderMode mode = ParticleRenderMode::billboard;
    ParticleRenderSemantic semantic = ParticleRenderSemantic::none;
    uint32_t material = 0;
    float opacity_scale = 1.0f;
    float blur_length = 0.0f;
    float deadspace_radians = 0.0f;
    bool tint_to_scene_ambient = false;
    int32_t sort_order = 0;
};

struct ParticleEmissionDef {
    ParticleEmissionMode mode = ParticleEmissionMode::continuous;
    ParticleSpawnMetric metric = ParticleSpawnMetric::per_second;
    float rate = 0.0f;
    ParticleCurveF32 rate_over_time;
    bool looping = true;
    bool trigger_on_effect_events = false;
    float effect_event_period = 0.0f;
};

struct ParticleEmitterAttachmentDef {
    // Scene/model-owned indices into the ModelInstance attachment transform
    // cache. Source adapters resolve their own node/name data before emitting
    // this source-neutral runtime row.
    ModelAttachmentPointIndex emitter_attachment_point = kInvalidModelAttachmentPointIndex;
    bool has_default_transform = false;
    glm::mat4 default_transform{1.0f};
    bool has_default_position = false;
    glm::vec3 default_position{0.0f};
    bool has_default_orientation = false;
    glm::quat default_orientation{1.0f, 0.0f, 0.0f, 0.0f};
    ModelAttachmentPointIndex target_attachment_point = kInvalidModelAttachmentPointIndex;
    bool has_default_target_offset = false;
    glm::vec3 default_target_offset{0.0f};
};

struct ParticleEmitterDef {
    std::string name;
    ParticleEmitterAttachmentDef attachment;
    ParticleEmissionDef emission;
    ParticleSpawnRegion region;
    ParticleSimulationSpace simulation_space = ParticleSimulationSpace::world;
    bool affected_by_wind = false;
    ParticleInitialStateDef initial;
    ParticleSpawnOverTimeDef spawn_over_time;
    ParticleOverLifeDef over_life;
    ParticleTargetingDef targeting;
    ParticleBeamDef beam;
    ParticleForceEventDef force_event;
    ParticleCollisionDef collision;
    ParticleRenderDef render;
    uint32_t max_particles = 0;
};

struct ParticleEffectDef {
    std::string name;
    std::vector<ParticleMaterialDef> materials;
    std::vector<ParticleEmitterDef> emitters;
};

} // namespace nw::render
