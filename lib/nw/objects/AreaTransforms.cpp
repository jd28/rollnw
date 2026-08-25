#include "AreaTransforms.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace nw {

AreaTileTransformStats build_area_tile_world_transforms(
    float tile_height,
    std::span<const AreaTileTransformInput> inputs,
    std::span<glm::mat4> outputs)
{
    AreaTileTransformStats stats;
    stats.input_count = inputs.size();

    const size_t count = std::min(inputs.size(), outputs.size());
    constexpr float tile_size = 10.0f;
    constexpr int32_t orientation_count = 4;

    for (size_t i = 0; i < count; ++i) {
        const auto& input = inputs[i];
        if (!std::isfinite(tile_height)
            || tile_height <= 0.0f
            || input.orientation < 0
            || input.orientation >= orientation_count) {
            outputs[i] = glm::mat4{1.0f};
            ++stats.rejected_count;
            continue;
        }

        const float world_x = static_cast<float>(input.x) * tile_size + tile_size * 0.5f;
        const float world_y = static_cast<float>(input.y) * tile_size + tile_size * 0.5f;
        const float world_z = static_cast<float>(input.height) * tile_height;
        const float angle = glm::radians(90.0f * static_cast<float>(input.orientation));

        outputs[i] = glm::translate(glm::mat4{1.0f}, glm::vec3{world_x, world_y, world_z})
            * glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
        ++stats.output_count;
    }

    stats.rejected_count += inputs.size() - count;
    return stats;
}

bool build_area_tile_world_transform(
    float tile_height,
    const AreaTileTransformInput& input,
    glm::mat4& output)
{
    const auto stats = build_area_tile_world_transforms(
        tile_height,
        std::span<const AreaTileTransformInput>{&input, 1},
        std::span<glm::mat4>{&output, 1});
    return stats.output_count == 1;
}

AreaObjectTransformStats build_area_object_world_transforms(
    std::span<const AreaObjectTransformInput> inputs,
    std::span<glm::mat4> outputs)
{
    AreaObjectTransformStats stats;
    stats.input_count = inputs.size();

    const size_t count = std::min(inputs.size(), outputs.size());
    constexpr float epsilon = 1.0e-5f;
    for (size_t i = 0; i < count; ++i) {
        const auto& input = inputs[i];
        const bool valid = std::isfinite(input.position.x)
            && std::isfinite(input.position.y)
            && std::isfinite(input.position.z)
            && std::isfinite(input.orientation.x)
            && std::isfinite(input.orientation.y)
            && std::isfinite(input.orientation.z)
            && std::isfinite(input.scale.x)
            && std::isfinite(input.scale.y)
            && std::isfinite(input.scale.z);
        if (!valid) {
            outputs[i] = glm::mat4{1.0f};
            ++stats.rejected_count;
            continue;
        }

        float angle = 0.0f;
        if (std::abs(input.orientation.x) > epsilon
            || std::abs(input.orientation.y) > epsilon) {
            angle = std::atan2(input.orientation.y, input.orientation.x);
        }
        outputs[i] = glm::scale(
            glm::translate(glm::mat4{1.0f}, input.position)
                * glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f})),
            input.scale);
        ++stats.output_count;
    }

    stats.rejected_count += inputs.size() - count;
    return stats;
}

bool build_area_object_world_transform(
    const AreaObjectTransformInput& input,
    glm::mat4& output)
{
    const auto stats = build_area_object_world_transforms(
        std::span<const AreaObjectTransformInput>{&input, 1},
        std::span<glm::mat4>{&output, 1});
    return stats.output_count == 1;
}

} // namespace nw
