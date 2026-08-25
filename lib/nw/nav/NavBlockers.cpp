#include "NavBlockers.hpp"

#include <glm/common.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace nw::nav {
namespace {

constexpr double tile_size = 10.0;
constexpr float geometry_epsilon = 0.001f;

struct Bounds {
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
};

struct CellRange {
    uint32_t min_x = 0;
    uint32_t min_y = 0;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
};

bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool triangle_bounds(const NavGeometry& geometry, size_t triangle, Bounds& output)
{
    const size_t offset = triangle * 3;
    if (offset + 2 >= geometry.indices.size()) return false;
    const uint32_t ia = geometry.indices[offset];
    const uint32_t ib = geometry.indices[offset + 1];
    const uint32_t ic = geometry.indices[offset + 2];
    if (ia >= geometry.vertices.size() || ib >= geometry.vertices.size()
        || ic >= geometry.vertices.size()) {
        return false;
    }
    const auto& a = geometry.vertices[ia];
    const auto& b = geometry.vertices[ib];
    const auto& c = geometry.vertices[ic];
    if (!finite(a) || !finite(b) || !finite(c)) return false;
    output.minimum = glm::min(a, glm::min(b, c));
    output.maximum = glm::max(a, glm::max(b, c));
    return true;
}

float cross_2d(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool point_on_segment(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b)
{
    if (std::abs(cross_2d(a, b, point)) > geometry_epsilon) return false;
    return point.x >= std::min(a.x, b.x) - geometry_epsilon
        && point.x <= std::max(a.x, b.x) + geometry_epsilon
        && point.y >= std::min(a.y, b.y) - geometry_epsilon
        && point.y <= std::max(a.y, b.y) + geometry_epsilon;
}

int orientation_sign(float value)
{
    if (value > geometry_epsilon) return 1;
    if (value < -geometry_epsilon) return -1;
    return 0;
}

bool segments_intersect(const glm::vec2& a, const glm::vec2& b,
    const glm::vec2& c, const glm::vec2& d)
{
    const int ab_c = orientation_sign(cross_2d(a, b, c));
    const int ab_d = orientation_sign(cross_2d(a, b, d));
    const int cd_a = orientation_sign(cross_2d(c, d, a));
    const int cd_b = orientation_sign(cross_2d(c, d, b));
    if (ab_c != ab_d && cd_a != cd_b) return true;
    return (ab_c == 0 && point_on_segment(c, a, b))
        || (ab_d == 0 && point_on_segment(d, a, b))
        || (cd_a == 0 && point_on_segment(a, c, d))
        || (cd_b == 0 && point_on_segment(b, c, d));
}

bool point_in_triangle(const glm::vec2& point, const std::array<glm::vec2, 3>& triangle)
{
    if (std::abs(cross_2d(triangle[0], triangle[1], triangle[2])) <= geometry_epsilon) {
        return false;
    }
    const int first = orientation_sign(cross_2d(triangle[0], triangle[1], point));
    const int second = orientation_sign(cross_2d(triangle[1], triangle[2], point));
    const int third = orientation_sign(cross_2d(triangle[2], triangle[0], point));
    const bool nonnegative = first >= 0 && second >= 0 && third >= 0;
    const bool nonpositive = first <= 0 && second <= 0 && third <= 0;
    return nonnegative || nonpositive;
}

bool projected_triangles_overlap(
    const std::array<glm::vec2, 3>& first,
    const std::array<glm::vec2, 3>& second)
{
    for (size_t a = 0; a < 3; ++a) {
        for (size_t b = 0; b < 3; ++b) {
            if (segments_intersect(first[a], first[(a + 1) % 3],
                    second[b], second[(b + 1) % 3])) {
                return true;
            }
        }
    }
    return point_in_triangle(first[0], second) || point_in_triangle(second[0], first);
}

bool triangles_overlap(const NavGeometry& base, size_t base_triangle,
    const Bounds& base_bounds, const NavGeometry& blockers,
    size_t blocker_triangle, const Bounds& blocker_bounds)
{
    if (base_bounds.maximum.x + geometry_epsilon < blocker_bounds.minimum.x
        || blocker_bounds.maximum.x + geometry_epsilon < base_bounds.minimum.x
        || base_bounds.maximum.y + geometry_epsilon < blocker_bounds.minimum.y
        || blocker_bounds.maximum.y + geometry_epsilon < base_bounds.minimum.y
        || base_bounds.maximum.z + geometry_epsilon < blocker_bounds.minimum.z
        || blocker_bounds.maximum.z + geometry_epsilon < base_bounds.minimum.z) {
        return false;
    }

    std::array<glm::vec2, 3> base_xy;
    std::array<glm::vec2, 3> blocker_xy;
    for (size_t vertex = 0; vertex < 3; ++vertex) {
        const auto& base_position = base.vertices[base.indices[base_triangle * 3 + vertex]];
        const auto& blocker_position = blockers.vertices[blockers.indices[blocker_triangle * 3 + vertex]];
        base_xy[vertex] = {base_position.x, base_position.y};
        blocker_xy[vertex] = {blocker_position.x, blocker_position.y};
    }
    return projected_triangles_overlap(base_xy, blocker_xy);
}

bool cell_range(const Bounds& bounds, uint32_t width, uint32_t height, CellRange& output)
{
    const double world_width = static_cast<double>(width) * tile_size;
    const double world_height = static_cast<double>(height) * tile_size;
    if (bounds.maximum.x < 0.0f || bounds.maximum.y < 0.0f
        || static_cast<double>(bounds.minimum.x) > world_width
        || static_cast<double>(bounds.minimum.y) > world_height) {
        return false;
    }
    const int64_t raw_min_x = static_cast<int64_t>(
        std::floor(static_cast<double>(bounds.minimum.x) / tile_size));
    const int64_t raw_min_y = static_cast<int64_t>(
        std::floor(static_cast<double>(bounds.minimum.y) / tile_size));
    const int64_t raw_max_x = static_cast<int64_t>(
        std::floor(static_cast<double>(bounds.maximum.x) / tile_size));
    const int64_t raw_max_y = static_cast<int64_t>(
        std::floor(static_cast<double>(bounds.maximum.y) / tile_size));
    output.min_x = static_cast<uint32_t>(std::clamp<int64_t>(raw_min_x, 0, width - 1));
    output.min_y = static_cast<uint32_t>(std::clamp<int64_t>(raw_min_y, 0, height - 1));
    output.max_x = static_cast<uint32_t>(std::clamp<int64_t>(raw_max_x, 0, width - 1));
    output.max_y = static_cast<uint32_t>(std::clamp<int64_t>(raw_max_y, 0, height - 1));
    return true;
}

} // namespace

NavStatus build_nav_blocker_overlaps(
    const NavGeometry& base,
    std::span<const uint8_t> surface_walkable,
    const NavGeometry& blockers,
    uint32_t area_width,
    uint32_t area_height,
    Vector<NavBlockerOverlapInput>& output,
    NavBlockerOverlapStats& stats)
{
    output.clear();
    stats = {};
    stats.base_triangle_count = base.triangle_count();
    stats.blocker_triangle_count = blockers.triangle_count();
    if (!base.valid() || !blockers.valid() || area_width == 0 || area_height == 0
        || stats.base_triangle_count > std::numeric_limits<uint32_t>::max()
        || stats.blocker_triangle_count > std::numeric_limits<uint32_t>::max()
        || area_width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
        || area_height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
        || static_cast<uint64_t>(area_width) * area_height > std::numeric_limits<size_t>::max()) {
        return NavStatus::rejected;
    }

    const size_t cell_count = static_cast<size_t>(area_width) * area_height;
    Vector<uint32_t> cell_counts(cell_count, 0);
    Vector<Bounds> base_bounds(stats.base_triangle_count);
    Vector<CellRange> base_cell_ranges(stats.base_triangle_count);
    Vector<uint8_t> accepted_base(stats.base_triangle_count, 0);
    for (size_t triangle = 0; triangle < stats.base_triangle_count; ++triangle) {
        if (base.surface[triangle] >= surface_walkable.size()
            || surface_walkable[base.surface[triangle]] == 0
            || !triangle_bounds(base, triangle, base_bounds[triangle])) {
            ++stats.rejected_base_triangle_count;
            continue;
        }
        auto& range = base_cell_ranges[triangle];
        if (!cell_range(base_bounds[triangle], area_width, area_height, range)) {
            ++stats.rejected_base_triangle_count;
            continue;
        }
        accepted_base[triangle] = 1;
        for (uint32_t y = range.min_y; y <= range.max_y; ++y) {
            for (uint32_t x = range.min_x; x <= range.max_x; ++x) {
                const size_t cell = static_cast<size_t>(y) * area_width + x;
                if (cell_counts[cell] == std::numeric_limits<uint32_t>::max()) {
                    return NavStatus::rejected;
                }
                ++cell_counts[cell];
            }
        }
    }

    Vector<uint32_t> cell_offsets(cell_count + 1, 0);
    for (size_t cell = 0; cell < cell_count; ++cell) {
        if (cell_counts[cell] > std::numeric_limits<uint32_t>::max() - cell_offsets[cell]) {
            return NavStatus::rejected;
        }
        cell_offsets[cell + 1] = cell_offsets[cell] + cell_counts[cell];
    }
    Vector<uint32_t> cell_triangles(cell_offsets.back());
    Vector<uint32_t> cursors = cell_offsets;
    for (size_t triangle = 0; triangle < stats.base_triangle_count; ++triangle) {
        if (!accepted_base[triangle]) continue;
        const auto& range = base_cell_ranges[triangle];
        for (uint32_t y = range.min_y; y <= range.max_y; ++y) {
            for (uint32_t x = range.min_x; x <= range.max_x; ++x) {
                const size_t cell = static_cast<size_t>(y) * area_width + x;
                cell_triangles[cursors[cell]++] = static_cast<uint32_t>(triangle);
            }
        }
    }

    Vector<uint32_t> candidate_marks(stats.base_triangle_count, 0);
    for (size_t blocker = 0; blocker < stats.blocker_triangle_count; ++blocker) {
        Bounds bounds;
        CellRange range;
        if (!triangle_bounds(blockers, blocker, bounds)
            || !cell_range(bounds, area_width, area_height, range)) {
            ++stats.rejected_blocker_triangle_count;
            continue;
        }
        const uint32_t generation = static_cast<uint32_t>(blocker) + 1;
        for (uint32_t y = range.min_y; y <= range.max_y; ++y) {
            for (uint32_t x = range.min_x; x <= range.max_x; ++x) {
                const size_t cell = static_cast<size_t>(y) * area_width + x;
                for (uint32_t row = cell_offsets[cell]; row < cell_offsets[cell + 1]; ++row) {
                    const uint32_t base_triangle = cell_triangles[row];
                    if (candidate_marks[base_triangle] == generation) continue;
                    candidate_marks[base_triangle] = generation;
                    ++stats.candidate_pair_count;
                    if (triangles_overlap(base, base_triangle, base_bounds[base_triangle],
                            blockers, blocker, bounds)) {
                        output.push_back({blockers.owner[blocker], base_triangle});
                    }
                }
            }
        }
    }

    std::sort(output.begin(), output.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.owner < rhs.owner || (lhs.owner == rhs.owner && lhs.triangle < rhs.triangle);
    });
    const size_t before_unique = output.size();
    output.erase(std::unique(output.begin(), output.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.owner == rhs.owner && lhs.triangle == rhs.triangle;
    }),
        output.end());
    stats.duplicate_count = before_unique - output.size();
    stats.overlap_count = output.size();
    return NavStatus::ok;
}

} // namespace nw::nav
