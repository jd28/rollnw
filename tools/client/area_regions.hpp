#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nw::toolset {

inline constexpr size_t k_maximum_area_region_points = 1024;

struct AreaRegionGeometry {
    glm::vec3 root_position{0.0f};
    std::vector<glm::vec3> local_points;
};

// Validates one ordered open or closed world-space region path. Open paths
// require one point and reject duplicate or intersecting segments. Closed
// paths additionally require three points, a valid closing edge, and nonzero
// XY area. Paths longer than k_maximum_area_region_points are rejected to
// bound the pairwise intersection pass. The input span is borrowed and never
// modified.
[[nodiscard]] bool validate_area_region_path(
    int32_t area_width,
    int32_t area_height,
    std::span<const glm::vec3> world_points,
    bool closed,
    std::string& diagnostic);

// Converts one world-space polygon batch into the native Area representation.
// All points must be finite and within [0, width*10] x [0, height*10]. New
// polygons require at least three unique, non-self-intersecting XY vertices.
[[nodiscard]] bool build_area_region_geometry(
    int32_t area_width,
    int32_t area_height,
    std::span<const glm::vec3> world_points,
    AreaRegionGeometry& output,
    std::string& diagnostic);

} // namespace nw::toolset
