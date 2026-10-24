#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cmath>

namespace nw::render {

struct Frustum {
    std::array<glm::vec4, 6> planes{};
    bool valid = false;
};

// Vulkan clip depth is 0..w. Invalid views retain all candidates.
inline Frustum make_frustum(const glm::mat4& view_projection) noexcept
{
    const auto row = [&view_projection](glm::length_t i) {
        return glm::vec4(view_projection[0][i], view_projection[1][i], view_projection[2][i], view_projection[3][i]);
    };
    const glm::vec4 row0 = row(0);
    const glm::vec4 row1 = row(1);
    const glm::vec4 row2 = row(2);
    const glm::vec4 row3 = row(3);

    Frustum result{};
    result.planes = {{
        row3 + row0,
        row3 - row0,
        row3 + row1,
        row3 - row1,
        row2,
        row3 - row2,
    }};

    result.valid = true;
    for (auto& plane : result.planes) {
        const float length = glm::length(glm::vec3{plane});
        if (!std::isfinite(length) || !std::isfinite(plane.w) || length <= 1.0e-6f) {
            result.valid = false;
            continue;
        }
        plane /= length;
    }
    return result;
}

inline bool bounds_intersects_frustum(const glm::vec3& minimum, const glm::vec3& maximum, const Frustum& frustum) noexcept
{
    if (!frustum.valid) {
        return true;
    }

    constexpr float kCullPadding = 0.25f;
    for (const glm::vec4& plane : frustum.planes) {
        const glm::vec3 normal{plane};
        const glm::vec3 positive{
            normal.x >= 0.0f ? maximum.x : minimum.x,
            normal.y >= 0.0f ? maximum.y : minimum.y,
            normal.z >= 0.0f ? maximum.z : minimum.z,
        };
        if (glm::dot(normal, positive) + plane.w < -kCullPadding) {
            return false;
        }
    }
    return true;
}

} // namespace nw::render
