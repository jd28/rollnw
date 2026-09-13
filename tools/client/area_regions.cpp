#include "area_regions.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <stdexcept>

namespace nw::toolset {
namespace {

constexpr float k_point_epsilon_squared = 1.0e-8f;
constexpr float k_area_epsilon = 1.0e-6f;

bool finite(glm::vec3 value) noexcept
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

float cross_xy(glm::vec2 a, glm::vec2 b, glm::vec2 c) noexcept
{
    const glm::vec2 ab = b - a;
    const glm::vec2 ac = c - a;
    return ab.x * ac.y - ab.y * ac.x;
}

bool point_on_segment(glm::vec2 point, glm::vec2 a, glm::vec2 b) noexcept
{
    if (std::abs(cross_xy(a, b, point)) > k_area_epsilon) {
        return false;
    }
    return point.x >= std::min(a.x, b.x) - k_area_epsilon
        && point.x <= std::max(a.x, b.x) + k_area_epsilon
        && point.y >= std::min(a.y, b.y) - k_area_epsilon
        && point.y <= std::max(a.y, b.y) + k_area_epsilon;
}

bool segments_intersect(
    glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) noexcept
{
    const float ab_c = cross_xy(a, b, c);
    const float ab_d = cross_xy(a, b, d);
    const float cd_a = cross_xy(c, d, a);
    const float cd_b = cross_xy(c, d, b);
    if (((ab_c > k_area_epsilon && ab_d < -k_area_epsilon)
            || (ab_c < -k_area_epsilon && ab_d > k_area_epsilon))
        && ((cd_a > k_area_epsilon && cd_b < -k_area_epsilon)
            || (cd_a < -k_area_epsilon && cd_b > k_area_epsilon))) {
        return true;
    }
    return (std::abs(ab_c) <= k_area_epsilon && point_on_segment(c, a, b))
        || (std::abs(ab_d) <= k_area_epsilon && point_on_segment(d, a, b))
        || (std::abs(cd_a) <= k_area_epsilon && point_on_segment(a, c, d))
        || (std::abs(cd_b) <= k_area_epsilon && point_on_segment(b, c, d));
}

} // namespace

bool validate_area_region_path(
    int32_t area_width,
    int32_t area_height,
    std::span<const glm::vec3> world_points,
    bool closed,
    std::string& diagnostic)
{
    diagnostic.clear();
    if (area_width <= 0 || area_height <= 0) {
        diagnostic = "Region requires a valid Area";
        return false;
    }
    if (world_points.size() > k_maximum_area_region_points) {
        diagnostic = "Region exceeds the 1024-point authoring limit";
        return false;
    }
    if (world_points.empty() || (closed && world_points.size() < 3)) {
        diagnostic = closed
            ? "Region requires at least three points"
            : "Region path requires a point";
        return false;
    }

    const float max_x = static_cast<float>(area_width) * 10.0f;
    const float max_y = static_cast<float>(area_height) * 10.0f;
    for (size_t index = 0; index < world_points.size(); ++index) {
        const glm::vec3 point = world_points[index];
        if (!finite(point)
            || point.x < 0.0f || point.x > max_x
            || point.y < 0.0f || point.y > max_y) {
            diagnostic = "Region point is non-finite or outside the Area";
            return false;
        }
        if (index == 0) continue;
        const glm::vec2 delta{
            point.x - world_points[index - 1].x,
            point.y - world_points[index - 1].y,
        };
        if (glm::dot(delta, delta) <= k_point_epsilon_squared) {
            diagnostic = "Region contains adjacent duplicate points";
            return false;
        }
    }
    if (closed) {
        const glm::vec2 delta{
            world_points.front().x - world_points.back().x,
            world_points.front().y - world_points.back().y,
        };
        if (glm::dot(delta, delta) <= k_point_epsilon_squared) {
            diagnostic = "Region contains adjacent duplicate points";
            return false;
        }
    }

    const size_t edge_count = closed
        ? world_points.size()
        : world_points.size() - 1;
    for (size_t first = 0; first < edge_count; ++first) {
        const size_t first_next = (first + 1) % world_points.size();
        for (size_t second = first + 1; second < edge_count; ++second) {
            const size_t second_next = (second + 1) % world_points.size();
            if (first == second || first_next == second
                || second_next == first) {
                continue;
            }
            if (segments_intersect(
                    glm::vec2{world_points[first]},
                    glm::vec2{world_points[first_next]},
                    glm::vec2{world_points[second]},
                    glm::vec2{world_points[second_next]})) {
                diagnostic = "Region polygon self-intersects";
                return false;
            }
        }
    }

    if (!closed) return true;
    double twice_area = 0.0;
    for (size_t index = 0; index < world_points.size(); ++index) {
        const auto& a = world_points[index];
        const auto& b = world_points[(index + 1) % world_points.size()];
        twice_area += static_cast<double>(a.x) * static_cast<double>(b.y)
            - static_cast<double>(b.x) * static_cast<double>(a.y);
    }
    if (!std::isfinite(twice_area)
        || std::abs(twice_area)
            <= static_cast<double>(k_area_epsilon)) {
        diagnostic = "Region polygon has zero XY area";
        return false;
    }
    return true;
}

bool build_area_region_geometry(
    int32_t area_width,
    int32_t area_height,
    std::span<const glm::vec3> world_points,
    AreaRegionGeometry& output,
    std::string& diagnostic)
{
    output = {};
    diagnostic.clear();
    if (!validate_area_region_path(
            area_width, area_height, world_points, true, diagnostic)) {
        return false;
    }

    try {
        output.root_position = world_points.front();
        output.local_points.reserve(world_points.size());
        for (const glm::vec3 point : world_points) {
            output.local_points.push_back(point - output.root_position);
        }
    } catch (const std::bad_alloc&) {
        output = {};
        diagnostic = "Region geometry allocation failed";
        return false;
    } catch (const std::length_error&) {
        output = {};
        diagnostic = "Region geometry exceeds container capacity";
        return false;
    }
    return true;
}

} // namespace nw::toolset
