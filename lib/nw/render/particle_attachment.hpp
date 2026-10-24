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

} // namespace nw::render
