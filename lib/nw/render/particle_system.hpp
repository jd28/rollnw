#pragma once

#include "particle_attachment.hpp"
#include "particle_compile.hpp"
#include "particle_render.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace nw::render {

struct ParticleCoreStorage {
    std::vector<glm::vec3> position;
    std::vector<glm::vec3> velocity;
    std::vector<glm::vec3> render_direction;
    std::vector<float> age;
    std::vector<float> lifetime;
    std::vector<float> mass;
    std::vector<float> size_x;
    std::vector<float> size_y;
    std::vector<float> size_x_begin;
    std::vector<float> size_x_end;
    std::vector<float> size_y_begin;
    std::vector<float> size_y_end;
    std::vector<float> rotation;
    std::vector<float> rotation_rate;
    std::vector<uint16_t> frame;
    std::vector<uint16_t> frame_offset;
    std::vector<uint32_t> color_rgba8;
    std::vector<glm::vec4> color_begin;
    std::vector<glm::vec4> color_end;
    std::vector<float> normalized_age;
    std::vector<uint16_t> emitter_id;
    std::vector<uint16_t> material_id;
};

struct ParticleBezierStorage {
    std::vector<glm::vec3> source_position;
    std::vector<glm::vec3> target_position;
    std::vector<glm::vec3> source_tangent;
    std::vector<glm::vec3> target_tangent;
};

struct ParticleBeamStorage {
    std::vector<glm::vec3> source_position;
    std::vector<glm::vec3> target_position;
    std::vector<uint16_t> subdivisions;
    std::vector<float> update_accumulator;
    std::vector<float> jitter_radius;
    std::vector<float> noise_scale;
};

struct ParticleAttachmentStorage {
    std::vector<glm::vec3> anchor_position;
    std::vector<glm::vec3> local_position;
};

struct ParticleStorage {
    ParticleCoreStorage core;
    ParticleBezierStorage bezier;
    ParticleBeamStorage beams;
    ParticleAttachmentStorage attachment;
    bool has_bezier = false;
    bool has_beams = false;
    bool has_attachment = false;
};

struct ParticleEmitterState {
    float time = 0.0f;
    float spawn_accumulator = 0.0f;
    float distance_accumulator = 0.0f;
    float single_shot_cooldown = 0.0f;
    float pulse_time_remaining = 0.0f;
    uint32_t pending_bursts = 0;
    uint32_t random_seed = 0;
    uint32_t body_spawn_counter = 0;
    glm::vec3 prev_world_pos{0.0f};
    glm::vec3 frame_delta{0.0f};
    glm::vec3 linear_velocity{0.0f};
    glm::mat4 world_transform{1.0f};
    glm::mat4 inverse_world_transform{1.0f};
    glm::vec3 target_point{0.0f};
    ParticleSpriteSheet sampled_sheet{};
    bool active = true;
};

struct ParticleExternalForces {
    glm::vec3 wind{0.0f};
};

struct ParticleCollisionQuery {
    bool hit = false;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

struct ParticleCollisionProvider {
    virtual ~ParticleCollisionProvider() = default;
    virtual ParticleCollisionQuery trace_particle(glm::vec3 from, glm::vec3 to, float radius) const = 0;
};

struct ParticleSimulationContext {
    ParticleExternalForces external{};
    const ParticleCollisionProvider* collision = nullptr;
};

struct ParticleForceEvent {
    uint16_t emitter_id = 0;
    glm::vec3 origin{0.0f};
    float radius = 0.0f;
    float length = 0.0f;
};

struct ParticleSystemInstance {
    const CompiledParticleEffect* effect = nullptr;
    ParticleStorage particles;
    std::vector<ParticleEmitterState> emitters;
    std::vector<ParticleEmitterAttachmentBinding> emitter_attachments;
    std::vector<ParticleEmitterAttachmentFrame> emitter_attachment_frames;
    std::vector<uint32_t> live_particles_per_emitter;
    std::vector<uint8_t> emitter_visible;
    ParticleRenderPacketList render_packets;
    std::vector<ParticleForceEvent> force_events;
    std::vector<uint32_t> tick_offsets;
    std::vector<uint32_t> sort_order;
    std::vector<uint32_t> sort_target;
    std::vector<uint64_t> sort_keys;
};

ParticleSystemInstance create_particle_system(const CompiledParticleEffect& effect);
void apply_particle_attachment_defaults(ParticleSystemInstance& system);
void kill_particle(ParticleSystemInstance& system, size_t particle_index);
void trigger_particle_emitter(ParticleSystemInstance& system, uint16_t emitter_id, uint32_t burst_count = 1);
void tick_particle_system(ParticleSystemInstance& system, float dt, const ParticleExternalForces& forces = {});
void tick_particle_system(ParticleSystemInstance& system, float dt, const ParticleSimulationContext& context);
std::span<const ParticleRenderPacket> build_particle_render_packets(ParticleSystemInstance& system);
std::span<const uint32_t> particle_render_packet_indices(
    const ParticleSystemInstance& system, const ParticleRenderPacket& packet) noexcept;
std::span<const ParticleForceEvent> get_particle_force_events(const ParticleSystemInstance& system);

} // namespace nw::render
