#pragma once

#include "model_attachment.hpp"
#include "model_instance_handle.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>

namespace nw::render {

struct ParticleEmitterAttachmentBinding {
    // Frame/runtime binding for an emitter anchored to a common model instance.
    // The scene owns handle validity. Attachment-point indices are the common
    // runtime path. Invalid or stale rows fall back to explicit default emitter
    // placement carried by the compiled effect.
    uint32_t emitter = std::numeric_limits<uint32_t>::max();
    ModelInstanceHandle owner_instance_handle;
    uint32_t owner_model_index = std::numeric_limits<uint32_t>::max();
    ModelAttachmentPointIndex emitter_attachment_point = kInvalidModelAttachmentPointIndex;
    ModelAttachmentPointIndex target_attachment_point = kInvalidModelAttachmentPointIndex;
};

struct ParticleEmitterAttachmentFrame {
    // Prepared frame data consumed by emitter sync. Invalid emitter rows are
    // dropped; missing emitter/target transforms are represented by false flags.
    uint32_t emitter = std::numeric_limits<uint32_t>::max();
    glm::mat4 owner_root_transform{1.0f};
    glm::mat4 emitter_world_transform{1.0f};
    glm::vec3 target_point{0.0f};
    bool has_emitter_world_transform = false;
    bool has_target_point = false;
};

} // namespace nw::render
