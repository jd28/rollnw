#pragma once

#include <nw/render/model.hpp>

#include <cstddef>
#include <span>

namespace nw::render {

struct RenderModelAttachmentRootTransformInput {
    // Transient, non-owning rows resolved by the scene from stable handles and
    // model indices before this batch transform runs. The records are not
    // retained; missing pointers are counted and dropped at this boundary.
    const ModelInstance* owner_instance = nullptr;
    const RenderModel* owner_model = nullptr;
    const RenderModel* child_model = nullptr;
    uint32_t owner_socket_index = kInvalidModelNodeIndex;
    uint32_t child_source_socket_index = kInvalidModelNodeIndex;
};

struct RenderModelAttachmentRootTransformOutput {
    glm::mat4 root_transform{1.0f};
    bool valid = false;
};

struct RenderModelAttachmentRootTransformStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t resolved_count = 0;
    size_t output_truncated_count = 0;
    size_t null_input_count = 0;
    size_t missing_owner_socket_count = 0;
    size_t missing_owner_transform_count = 0;
    size_t missing_child_socket_count = 0;
};

// Batch transform from common RenderModel socket rows plus ModelInstance node
// world rows to child root transforms. Owner socket and owner node transform are
// required. Child source socket is optional; missing/out-of-range child sockets
// use identity offset and are counted separately from drops.
RenderModelAttachmentRootTransformStats build_render_model_attachment_root_transforms(
    std::span<const RenderModelAttachmentRootTransformInput> inputs,
    std::span<RenderModelAttachmentRootTransformOutput> outputs) noexcept;

// Degenerate batch wrapper for call sites that naturally update one binding.
bool build_render_model_attachment_root_transform(
    const RenderModelAttachmentRootTransformInput& input,
    glm::mat4& out_root_transform) noexcept;

} // namespace nw::render
