#pragma once

#include "particle_def.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace nw::render {

enum class ParticleRenderPathKind : uint8_t {
    none,
    bezier,
    beam,
};

struct ParticleRenderPath {
    ParticleRenderPathKind kind = ParticleRenderPathKind::none;
    glm::vec3 source{0.0f};
    glm::vec3 control1{0.0f};
    glm::vec3 control2{0.0f};
    glm::vec3 target{0.0f};
    uint16_t subdivisions = 0;
};

struct ParticleRenderPacket {
    ParticleRenderMode mode = ParticleRenderMode::billboard;
    ParticleRenderSemantic semantic = ParticleRenderSemantic::none;
    uint32_t material = 0;
    // Offset/count into ParticleSystemInstance::sort_order for this frame.
    // The particle SoA storage remains in simulation order.
    uint32_t begin = 0;
    uint32_t count = 0;
    ParticleBlendMode blend = ParticleBlendMode::alpha;
    ParticleSpriteSheet sheet{};
    ParticleRenderPath path{};
    bool double_sided = false;
    bool transparent = true;
    bool sort_back_to_front = false;
    bool preserve_sequence = false;
    bool uniform_color = false;
    bool uniform_frame = false;
    bool uniform_rotation = false;
    bool tint_to_scene_ambient = false;
    float opacity_scale = 1.0f;
    float blur_length = 0.0f;
    float deadspace_radians = 0.0f;
    float max_half_width = 0.0f;
};

struct ParticleRenderPacketList {
    std::vector<ParticleRenderPacket> packets;

    std::span<const ParticleRenderPacket> span() const noexcept { return packets; }
};

} // namespace nw::render
