#pragma once

#include <nw/render/model.hpp>

#include <cstddef>
#include <span>

namespace nw::render {

enum class ModelAttachmentOrientationPolicy : uint8_t {
    socket,
    owner_space_placement,
    owner_root,
};

enum class ModelAttachmentSourceOffsetPolicy : uint8_t {
    none,
    socket_bind,
    socket_bind_or_root_translation,
};

struct ModelInstanceAttachmentBinding {
    // Stable scene/runtime row. Names are resolved to socket indices during
    // setup; frame evaluation consumes only handles, indices, and policy data.
    ModelInstanceHandle child_instance_handle;
    ModelInstanceHandle owner_instance_handle;
    uint32_t owner_socket_index = kInvalidModelNodeIndex;
    uint32_t child_source_socket_index = kInvalidModelNodeIndex;
    glm::mat4 child_local_transform{1.0f};
    glm::vec3 child_root_bind_translation{0.0f};
    float child_local_scale = 1.0f;
    ModelAttachmentOrientationPolicy orientation = ModelAttachmentOrientationPolicy::socket;
    ModelAttachmentSourceOffsetPolicy source_offset = ModelAttachmentSourceOffsetPolicy::socket_bind;
};

struct ModelAttachmentRootTransformInput {
    // Transient, non-owning rows resolved by the scene from stable handles and
    // asset socket tables before this batch transform runs. The records are not
    // retained; missing inputs are counted and dropped at this boundary.
    const ModelInstance* owner_instance = nullptr;
    std::span<const ModelSocket> owner_sockets;
    std::span<const ModelSocket> child_sockets;
    uint32_t owner_socket_index = kInvalidModelNodeIndex;
    uint32_t child_source_socket_index = kInvalidModelNodeIndex;
    glm::mat4 child_local_transform{1.0f};
    glm::vec3 child_root_bind_translation{0.0f};
    float child_local_scale = 1.0f;
    ModelAttachmentOrientationPolicy orientation = ModelAttachmentOrientationPolicy::socket;
    ModelAttachmentSourceOffsetPolicy source_offset = ModelAttachmentSourceOffsetPolicy::socket_bind;
};

struct ModelAttachmentRootTransformOutput {
    glm::mat4 root_transform{1.0f};
    bool valid = false;
};

struct ModelAttachmentRootTransformStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t resolved_count = 0;
    size_t output_truncated_count = 0;
    size_t null_input_count = 0;
    size_t missing_owner_socket_count = 0;
    size_t missing_owner_transform_count = 0;
    size_t missing_child_socket_count = 0;
    size_t invalid_input_transform_count = 0;
    size_t invalid_socket_transform_count = 0;
    size_t invalid_local_scale_count = 0;
    size_t invalid_policy_count = 0;
};

// Batch transform from common socket rows plus ModelInstance node world rows to
// child root transforms. Owner socket and owner node transform are required.
// Socket orientation applies child_local_transform after the resolved socket.
// Owner-space placement preserves source formats that apply placement before
// the socket. Optional child sockets and compatibility root translations are
// selected by explicit source-offset policy. Invalid policies, scales, and
// transforms are rejected; missing optional child sockets use the documented
// policy fallback.
ModelAttachmentRootTransformStats build_model_attachment_root_transforms(
    std::span<const ModelAttachmentRootTransformInput> inputs,
    std::span<ModelAttachmentRootTransformOutput> outputs) noexcept;

// Degenerate batch wrapper for call sites that naturally update one binding.
bool build_model_attachment_root_transform(
    const ModelAttachmentRootTransformInput& input,
    glm::mat4& out_root_transform) noexcept;

} // namespace nw::render
