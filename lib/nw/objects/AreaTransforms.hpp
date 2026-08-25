#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace nw {

struct AreaTileTransformInput {
    int32_t x = 0;
    int32_t y = 0;
    int32_t height = 0;
    int32_t orientation = 0;
};

struct AreaTileTransformStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t rejected_count = 0;
};

/// Builds NWN tile-local to area-world transforms. Inputs and outputs are
/// contiguous and correspond by index. Invalid rows receive the identity matrix.
AreaTileTransformStats build_area_tile_world_transforms(
    float tile_height,
    std::span<const AreaTileTransformInput> inputs,
    std::span<glm::mat4> outputs);

/// Degenerate one-row wrapper over build_area_tile_world_transforms.
bool build_area_tile_world_transform(
    float tile_height,
    const AreaTileTransformInput& input,
    glm::mat4& output);

struct AreaObjectTransformInput {
    glm::vec3 position{0.0f};
    glm::vec3 orientation{1.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct AreaObjectTransformStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t rejected_count = 0;
};

/// Builds placed-object local to area-world transforms. Inputs and outputs are
/// contiguous and correspond by index. Non-finite rows receive identity.
AreaObjectTransformStats build_area_object_world_transforms(
    std::span<const AreaObjectTransformInput> inputs,
    std::span<glm::mat4> outputs);

/// Degenerate one-row wrapper over build_area_object_world_transforms.
bool build_area_object_world_transform(
    const AreaObjectTransformInput& input,
    glm::mat4& output);

} // namespace nw
