#include "model_instance_attachment.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace nw::render {

namespace {

bool build_render_model_attachment_root_transform_impl(
    const RenderModelAttachmentRootTransformInput& input,
    glm::mat4& out_root_transform,
    RenderModelAttachmentRootTransformStats& stats) noexcept
{
    if (!input.owner_instance || !input.owner_model || !input.child_model) {
        ++stats.null_input_count;
        return false;
    }

    const auto* owner_socket = input.owner_model->socket(input.owner_socket_index);
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

    if (const auto* child_socket = input.child_model->socket(input.child_source_socket_index)) {
        out_root_transform *= glm::inverse(child_socket->bind_transform);
    } else if (input.child_source_socket_index != kInvalidModelNodeIndex) {
        ++stats.missing_child_socket_count;
    }
    return true;
}

} // namespace

RenderModelAttachmentRootTransformStats build_render_model_attachment_root_transforms(
    std::span<const RenderModelAttachmentRootTransformInput> inputs,
    std::span<RenderModelAttachmentRootTransformOutput> outputs) noexcept
{
    RenderModelAttachmentRootTransformStats stats{};
    stats.input_count = inputs.size();
    stats.output_count = outputs.size();

    const size_t count = std::min(inputs.size(), outputs.size());
    stats.output_truncated_count = inputs.size() - count;
    for (size_t i = 0; i < count; ++i) {
        auto& output = outputs[i];
        output.root_transform = glm::mat4{1.0f};
        output.valid = build_render_model_attachment_root_transform_impl(inputs[i], output.root_transform, stats);
        if (output.valid) {
            ++stats.resolved_count;
        }
    }

    return stats;
}

bool build_render_model_attachment_root_transform(
    const RenderModelAttachmentRootTransformInput& input,
    glm::mat4& out_root_transform) noexcept
{
    std::array<RenderModelAttachmentRootTransformInput, 1> inputs{input};
    std::array<RenderModelAttachmentRootTransformOutput, 1> outputs{};
    const auto stats = build_render_model_attachment_root_transforms(inputs, outputs);
    if (stats.resolved_count == 0 || !outputs[0].valid) {
        return false;
    }
    out_root_transform = outputs[0].root_transform;
    return true;
}

} // namespace nw::render
