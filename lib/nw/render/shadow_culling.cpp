#include "shadow_culling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace nw::render {
namespace {

bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool intersects_clip(const glm::vec3& minimum, const glm::vec3& maximum)
{
    constexpr float padding = 0.05f;
    return maximum.x >= -1.0f - padding && minimum.x <= 1.0f + padding
        && maximum.y >= -1.0f - padding && minimum.y <= 1.0f + padding
        && maximum.z >= -padding && minimum.z <= 1.0f + padding;
}

bool corners_intersect_clip(const Bounds& bounds, const glm::mat4& matrix)
{
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (const float z : {bounds.min.z, bounds.max.z}) {
        for (const float y : {bounds.min.y, bounds.max.y}) {
            for (const float x : {bounds.min.x, bounds.max.x}) {
                const glm::vec4 clip = matrix * glm::vec4{x, y, z, 1.0f};
                if (std::abs(clip.w) <= 1.0e-6f) continue;
                const glm::vec3 ndc = glm::vec3{clip} / clip.w;
                minimum = glm::min(minimum, ndc);
                maximum = glm::max(maximum, ndc);
            }
        }
    }
    return intersects_clip(minimum, maximum);
}

bool orthographic(const glm::mat4& matrix)
{
    if (matrix[0].w != 0.0f || matrix[1].w != 0.0f
        || matrix[2].w != 0.0f || matrix[3].w != 1.0f) return false;
    for (glm::length_t column = 0; column < 4; ++column) {
        if (!finite(glm::vec3{matrix[column]})) return false;
    }
    return true;
}

std::optional<bool> intervals_intersect_clip(const Bounds& bounds, const glm::mat4& matrix)
{
    if (!finite(bounds.min) || !finite(bounds.max)) return std::nullopt;
    const glm::vec3 x0 = glm::vec3{matrix[0]} * bounds.min.x;
    const glm::vec3 x1 = glm::vec3{matrix[0]} * bounds.max.x;
    const glm::vec3 y0 = glm::vec3{matrix[1]} * bounds.min.y;
    const glm::vec3 y1 = glm::vec3{matrix[1]} * bounds.max.y;
    const glm::vec3 z0 = glm::vec3{matrix[2]} * bounds.min.z;
    const glm::vec3 z1 = glm::vec3{matrix[2]} * bounds.max.z;
    const glm::vec3 translation{matrix[3]};
    // Each coordinate's extrema occur at signed column endpoints. Preserve
    // GLM's ((x + y) + (z + translation)) grouping, including float rounding.
    const glm::vec3 minimum = (glm::min(x0, x1) + glm::min(y0, y1))
        + (glm::min(z0, z1) + translation);
    const glm::vec3 maximum = (glm::max(x0, x1) + glm::max(y0, y1))
        + (glm::max(z0, z1) + translation);
    if (!finite(minimum) || !finite(maximum)) return std::nullopt;
    return intersects_clip(minimum, maximum);
}

} // namespace

bool collect_shadow_visibility_masks(std::span<const Bounds> bounds,
    std::span<const glm::mat4> world_to_shadow, std::span<uint8_t> masks)
{
    if (masks.size() != bounds.size() || world_to_shadow.size() > 8u) return false;
    std::fill(masks.begin(), masks.end(), 0u);
    if (bounds.empty()) return true;

    Bounds batch_bounds = bounds.front();
    bool ordered_finite = true;
    for (const auto& row : bounds) {
        ordered_finite &= finite(row.min) && finite(row.max)
            && glm::all(glm::lessThanEqual(row.min, row.max));
        batch_bounds.min = glm::min(batch_bounds.min, row.min);
        batch_bounds.max = glm::max(batch_bounds.max, row.max);
    }
    for (size_t view = 0; view < world_to_shadow.size(); ++view) {
        const auto& matrix = world_to_shadow[view];
        const auto bit = static_cast<uint8_t>(1u << view);
        if (orthographic(matrix)) {
            // With constant W, an enclosing bound also encloses every child's
            // projected interval. Perspective division does not have this rule.
            if (ordered_finite && intervals_intersect_clip(batch_bounds, matrix) == false) continue;
            for (size_t index = 0; index < bounds.size(); ++index) {
                const auto visible = intervals_intersect_clip(bounds[index], matrix);
                if (visible ? *visible : corners_intersect_clip(bounds[index], matrix)) masks[index] |= bit;
            }
        } else {
            for (size_t index = 0; index < bounds.size(); ++index) {
                if (corners_intersect_clip(bounds[index], matrix)) masks[index] |= bit;
            }
        }
    }
    return true;
}

} // namespace nw::render
