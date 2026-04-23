#pragma once

#include "../render/particle_system.hpp"
#include "../render/particle_def.hpp"
#include "Mdl.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace nw::model {

struct ParticleImportWarning {
    std::string emitter;
    std::string field;
    std::string message;
};

struct ParticleImportEmitterInit {
    uint32_t emitter = 0;
    std::string emitter_node_name;
    bool has_default_transform = false;
    glm::mat4 default_transform{1.0f};
    bool has_default_position = false;
    glm::vec3 default_position{0.0f};
    bool has_default_orientation = false;
    glm::quat default_orientation{1.0f, 0.0f, 0.0f, 0.0f};
    std::string target_node_name;
    bool has_default_target_offset = false;
    glm::vec3 default_target_offset{0.0f};
};

struct ParticleImportEffectEvent {
    float time = 0.0f;
    uint32_t burst_count = 1;
};

struct ParticleImportResult {
    nw::render::ParticleEffectDef effect;
    std::vector<ParticleImportWarning> warnings;
    std::vector<ParticleImportEmitterInit> emitter_inits;
    std::vector<ParticleImportEffectEvent> effect_events;
};

[[nodiscard]]
ParticleImportResult import_particle_effect(const Mdl& mdl, StringView animation_name = {}, bool fallback_all_animations = true);
void apply_particle_import_inits(const ParticleImportResult& import, nw::render::ParticleSystemInstance& system);
void apply_particle_import_events(const ParticleImportResult& import, nw::render::ParticleSystemInstance& system,
    float previous_time, float current_time, float animation_length, bool include_start_time = false);

} // namespace nw::model
