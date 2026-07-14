#include "model_instance_attachment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace nw::render {

namespace {

bool finite_mat4(const glm::mat4& value) noexcept
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) {
                return false;
            }
        }
    }
    return true;
}

bool normalized_root_rotation(const glm::mat4& root, glm::quat& out_rotation) noexcept
{
    glm::mat3 basis{root};
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(basis[i]);
        if (!std::isfinite(length) || length <= 0.0f) {
            return false;
        }
        basis[i] /= length;
    }
    out_rotation = glm::normalize(glm::quat_cast(basis));
    return std::isfinite(out_rotation.x)
        && std::isfinite(out_rotation.y)
        && std::isfinite(out_rotation.z)
        && std::isfinite(out_rotation.w);
}

const ModelSocket* socket_at(std::span<const ModelSocket> sockets, uint32_t index) noexcept
{
    return index < sockets.size() ? &sockets[index] : nullptr;
}

bool build_model_attachment_root_transform_impl(
    const ModelAttachmentRootTransformInput& input,
    glm::mat4& out_root_transform,
    ModelAttachmentRootTransformStats& stats) noexcept
{
    if (!input.owner_instance) {
        ++stats.null_input_count;
        return false;
    }
    if (!std::isfinite(input.child_local_scale) || input.child_local_scale <= 0.0f) {
        ++stats.invalid_local_scale_count;
        return false;
    }
    if (!finite_mat4(input.owner_instance->root_transform)
        || !finite_mat4(input.child_local_transform)) {
        ++stats.invalid_input_transform_count;
        return false;
    }

    const auto* owner_socket = socket_at(input.owner_sockets, input.owner_socket_index);
    if (!owner_socket
        || owner_socket->source_node_index == kInvalidModelNodeIndex
        || owner_socket->source_node_index > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        ++stats.missing_owner_socket_count;
        return false;
    }

    if (!model_instance_node_world_transform(
            *input.owner_instance, static_cast<int32_t>(owner_socket->source_node_index), out_root_transform)) {
        ++stats.missing_owner_transform_count;
        return false;
    }
    if (!finite_mat4(out_root_transform)) {
        ++stats.invalid_socket_transform_count;
        return false;
    }

    switch (input.orientation) {
    case ModelAttachmentOrientationPolicy::socket:
        out_root_transform *= input.child_local_transform;
        break;
    case ModelAttachmentOrientationPolicy::owner_space_placement: {
        const float determinant = glm::determinant(glm::mat3{input.owner_instance->root_transform});
        if (!std::isfinite(determinant) || std::fabs(determinant) <= 1.0e-8f) {
            ++stats.invalid_input_transform_count;
            return false;
        }
        const glm::mat4 owner_socket_local = glm::inverse(input.owner_instance->root_transform) * out_root_transform;
        out_root_transform = input.owner_instance->root_transform * input.child_local_transform * owner_socket_local;
        break;
    }
    case ModelAttachmentOrientationPolicy::owner_root: {
        glm::quat root_rotation{};
        if (!normalized_root_rotation(input.owner_instance->root_transform, root_rotation)) {
            ++stats.invalid_input_transform_count;
            return false;
        }
        out_root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{out_root_transform[3]})
            * glm::mat4_cast(root_rotation)
            * input.child_local_transform;
        break;
    }
    default:
        ++stats.invalid_policy_count;
        return false;
    }

    if (input.child_local_scale != 1.0f) {
        out_root_transform *= glm::scale(glm::mat4{1.0f}, glm::vec3{input.child_local_scale});
    }

    const auto* child_socket = socket_at(input.child_sockets, input.child_source_socket_index);
    switch (input.source_offset) {
    case ModelAttachmentSourceOffsetPolicy::none:
        break;
    case ModelAttachmentSourceOffsetPolicy::socket_bind:
        if (child_socket) {
            const glm::mat4 inverse_bind = glm::inverse(child_socket->bind_transform);
            if (!finite_mat4(inverse_bind)) {
                ++stats.invalid_socket_transform_count;
                return false;
            }
            out_root_transform *= inverse_bind;
        } else if (input.child_source_socket_index != kInvalidModelNodeIndex) {
            ++stats.missing_child_socket_count;
        }
        break;
    case ModelAttachmentSourceOffsetPolicy::socket_bind_or_root_translation:
        if (child_socket) {
            const glm::mat4 inverse_bind = glm::inverse(child_socket->bind_transform);
            if (!finite_mat4(inverse_bind)) {
                ++stats.invalid_socket_transform_count;
                return false;
            }
            out_root_transform *= inverse_bind;
        } else {
            if (input.child_source_socket_index != kInvalidModelNodeIndex) {
                ++stats.missing_child_socket_count;
            }
            out_root_transform *= glm::translate(glm::mat4{1.0f}, -input.child_root_bind_translation);
        }
        break;
    default:
        ++stats.invalid_policy_count;
        return false;
    }
    if (!finite_mat4(out_root_transform)) {
        ++stats.invalid_socket_transform_count;
        return false;
    }
    return true;
}

} // namespace

ModelAttachmentRootTransformStats build_model_attachment_root_transforms(
    std::span<const ModelAttachmentRootTransformInput> inputs,
    std::span<ModelAttachmentRootTransformOutput> outputs) noexcept
{
    ModelAttachmentRootTransformStats stats{};
    stats.input_count = inputs.size();
    stats.output_count = outputs.size();

    const size_t count = std::min(inputs.size(), outputs.size());
    stats.output_truncated_count = inputs.size() - count;
    for (size_t i = 0; i < count; ++i) {
        auto& output = outputs[i];
        output.root_transform = glm::mat4{1.0f};
        output.valid = build_model_attachment_root_transform_impl(inputs[i], output.root_transform, stats);
        if (output.valid) {
            ++stats.resolved_count;
        }
    }

    return stats;
}

bool build_model_attachment_root_transform(
    const ModelAttachmentRootTransformInput& input,
    glm::mat4& out_root_transform) noexcept
{
    std::array<ModelAttachmentRootTransformInput, 1> inputs{input};
    std::array<ModelAttachmentRootTransformOutput, 1> outputs{};
    const auto stats = build_model_attachment_root_transforms(inputs, outputs);
    if (stats.resolved_count == 0 || !outputs[0].valid) {
        return false;
    }
    out_root_transform = outputs[0].root_transform;
    return true;
}

} // namespace nw::render
