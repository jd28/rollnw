#pragma once

#include "particle_def.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nw::render {

inline constexpr uint32_t kMaxCompiledParticlesPerEmitter = 4096;
inline constexpr uint32_t kMaxCompiledParticlesPerEffect = 65536;
inline constexpr int32_t kMaxParticleRenderSortOrder = 255;

enum class CompiledParticleKernel {
    sprite_basic_constant,
    sprite_basic,
    sprite_target_gravity,
    sprite_target_bezier,
    linked_chain,
    beam_lightning,
    mesh_basic,
};

enum class CompiledParticleTrackMode : uint8_t {
    constant,
};

struct CompiledParticleScalarTrack {
    std::vector<ParticleCurveKeyF32> keys;
};

struct CompiledParticleKeyedScalarTrack {
    std::vector<ParticleCurveKeyF32> keys;
};

struct CompiledParticleKeyedColorTrack {
    std::vector<ParticleGradientKey> keys;
};

struct CompiledParticleColorTrack {
    std::vector<ParticleGradientKey> keys;
};

struct CompiledParticleFeature {
    static constexpr uint32_t update_size = 1u << 0;
    static constexpr uint32_t update_color = 1u << 1;
    static constexpr uint32_t update_rotation = 1u << 2;
    static constexpr uint32_t update_frame = 1u << 3;
    static constexpr uint32_t sample_spawn_size = 1u << 4;
    static constexpr uint32_t sample_spawn_color = 1u << 5;
    static constexpr uint32_t sample_spawn_motion = 1u << 6;
    static constexpr uint32_t sample_spawn_frame = 1u << 7;
    static constexpr uint32_t spawn_size_x_lerp = 1u << 8;
    static constexpr uint32_t spawn_size_y_lerp = 1u << 9;
    static constexpr uint32_t spawn_color_lerp = 1u << 10;
};

struct CompiledParticleStorage {
    static constexpr uint32_t bezier = 1u << 0;
    static constexpr uint32_t beams = 1u << 1;
    static constexpr uint32_t attachment = 1u << 2;
};

struct CompiledParticleMaterial {
    ParticleBlendMode blend = ParticleBlendMode::alpha;
    bool double_sided = false;
    uint32_t texture = 0;
    uint32_t mesh = 0;
    ParticleSpriteSheet sheet;
};

struct CompiledParticleEmitter {
    CompiledParticleKernel kernel = CompiledParticleKernel::sprite_basic;
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
    CompiledParticleKeyedScalarTrack emission_rate_track;
    CompiledParticleKeyedScalarTrack alpha_start_track;
    CompiledParticleKeyedScalarTrack alpha_end_track;
    CompiledParticleKeyedScalarTrack lifetime_track;
    CompiledParticleKeyedScalarTrack speed_track;
    CompiledParticleKeyedScalarTrack speed_random_track;
    CompiledParticleKeyedScalarTrack mass_track;
    CompiledParticleKeyedScalarTrack rotation_rate_track;
    CompiledParticleKeyedScalarTrack spread_track;
    CompiledParticleKeyedScalarTrack sheet_frame_begin_track;
    CompiledParticleKeyedScalarTrack sheet_frame_end_track;
    CompiledParticleKeyedScalarTrack sheet_fps_track;
    CompiledParticleKeyedScalarTrack sheet_random_start_track;
    CompiledParticleKeyedScalarTrack size_start_x_track;
    CompiledParticleKeyedScalarTrack size_end_x_track;
    CompiledParticleKeyedScalarTrack size_start_y_track;
    CompiledParticleKeyedScalarTrack size_end_y_track;
    CompiledParticleKeyedColorTrack color_start_track;
    CompiledParticleKeyedColorTrack color_end_track;
    CompiledParticleScalarTrack alpha_track;
    CompiledParticleScalarTrack size_x_track;
    CompiledParticleScalarTrack size_y_track;
    CompiledParticleScalarTrack rotation_track;
    CompiledParticleColorTrack color_track;
    uint32_t features = 0;
    uint32_t material = 0;
    uint32_t max_particles = 0;
};

struct CompiledParticleEffect {
    std::vector<CompiledParticleMaterial> materials;
    std::vector<CompiledParticleEmitter> emitters;
    uint32_t storage = 0;
    uint32_t max_particles_total = 0;
};

struct ParticleCompileWarning {
    uint32_t emitter = 0;
    std::string message;
};

struct ParticleCompileResult {
    CompiledParticleEffect effect;
    std::vector<ParticleCompileWarning> warnings;
};

ParticleCompileResult compile_particle_effect(const ParticleEffectDef& def);

} // namespace nw::render
