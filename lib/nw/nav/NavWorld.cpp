#include "NavWorld.hpp"

#include "NavGeometry.hpp"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>

#include <glm/common.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace nw::nav {
namespace {

constexpr float k_quantization_step = 0.001f;
constexpr float k_tile_size = 10.0f;
constexpr int64_t k_tile_quantized_size = 10000;
constexpr float k_walkable_height = 2.0f;
constexpr float k_walkable_radius = 0.0f;
constexpr float k_walkable_climb = 0.5f;
constexpr int64_t k_walkable_climb_quantized = 500;
constexpr unsigned short k_walkable_flag = 0x1;
constexpr unsigned short k_mesh_null_index = 0xffff;
constexpr unsigned short k_portal_mask = 0x8000;
constexpr int k_max_path_polygons = 2048;
constexpr int k_max_path_corners = 512;
constexpr int k_max_move_polygons = 32;
constexpr size_t k_max_clearance_iterations = 4;
constexpr float k_clearance_epsilon = 1.0e-4f;
constexpr int k_query_nodes = 65535;
constexpr std::array<float, 3> k_nearest_half_extents{10.0f, 50.0f, 10.0f};

bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::array<float, 3> to_detour(const glm::vec3& value)
{
    return {value.x, value.z, value.y};
}

glm::vec3 from_detour(const float* value)
{
    return {value[0], value[2], value[1]};
}

unsigned short portal_direction(uint32_t owner, uint32_t neighbor, uint32_t width)
{
    const uint32_t x = owner % width;
    const uint32_t y = owner / width;
    const uint32_t nx = neighbor % width;
    const uint32_t ny = neighbor / width;
    if (nx + 1 == x && ny == y) return k_portal_mask | 0;
    if (nx == x && ny == y + 1) return k_portal_mask | 1;
    if (nx == x + 1 && ny == y) return k_portal_mask | 2;
    if (nx == x && ny + 1 == y) return k_portal_mask | 3;
    return k_mesh_null_index;
}

struct BoundaryEdge {
    uint64_t seam = 0;
    int64_t along_min = 0;
    int64_t along_max = 0;
    int64_t height_at_min = 0;
    int64_t height_at_max = 0;
    size_t source = 0;
    uint32_t owner = 0;
    uint8_t side = 0;
};

bool quantize_world(float value, int64_t& output)
{
    const double quantized = std::round(
        static_cast<double>(value) / static_cast<double>(k_quantization_step));
    if (!std::isfinite(quantized)
        || quantized < static_cast<double>(std::numeric_limits<int64_t>::min())
        || quantized > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    output = static_cast<int64_t>(quantized);
    return true;
}

bool make_boundary_edge(
    const NavGeometry& geometry,
    size_t source,
    uint32_t area_width,
    uint32_t area_height,
    BoundaryEdge& output)
{
    const size_t triangle = source / 3;
    const size_t triangle_offset = triangle * 3;
    const size_t local_edge = source % 3;
    const uint32_t first_index = geometry.indices[source];
    const uint32_t second_index = geometry.indices[triangle_offset + (local_edge + 1) % 3];
    if (triangle >= geometry.owner.size()
        || first_index >= geometry.vertices.size()
        || second_index >= geometry.vertices.size()) {
        return false;
    }

    const uint32_t owner = geometry.owner[triangle];
    const uint32_t tile_x = owner % area_width;
    const uint32_t tile_y = owner / area_width;
    int64_t first_x = 0;
    int64_t first_y = 0;
    int64_t second_x = 0;
    int64_t second_y = 0;
    int64_t first_z = 0;
    int64_t second_z = 0;
    if (!quantize_world(geometry.vertices[first_index].x, first_x)
        || !quantize_world(geometry.vertices[first_index].y, first_y)
        || !quantize_world(geometry.vertices[second_index].x, second_x)
        || !quantize_world(geometry.vertices[second_index].y, second_y)
        || !quantize_world(geometry.vertices[first_index].z, first_z)
        || !quantize_world(geometry.vertices[second_index].z, second_z)) {
        return false;
    }

    const int64_t minimum_x = static_cast<int64_t>(tile_x) * k_tile_quantized_size;
    const int64_t minimum_y = static_cast<int64_t>(tile_y) * k_tile_quantized_size;
    uint32_t neighbor = UINT32_MAX;
    int64_t along_first = 0;
    int64_t along_second = 0;
    if (first_x == minimum_x && second_x == minimum_x && tile_x > 0) {
        neighbor = owner - 1;
        output.side = 0;
        along_first = first_y;
        along_second = second_y;
    } else if (first_y == minimum_y + k_tile_quantized_size
        && second_y == minimum_y + k_tile_quantized_size
        && tile_y + 1 < area_height) {
        neighbor = owner + area_width;
        output.side = 1;
        along_first = first_x;
        along_second = second_x;
    } else if (first_x == minimum_x + k_tile_quantized_size
        && second_x == minimum_x + k_tile_quantized_size
        && tile_x + 1 < area_width) {
        neighbor = owner + 1;
        output.side = 2;
        along_first = first_y;
        along_second = second_y;
    } else if (first_y == minimum_y && second_y == minimum_y && tile_y > 0) {
        neighbor = owner - area_width;
        output.side = 3;
        along_first = first_x;
        along_second = second_x;
    } else {
        return false;
    }

    output.along_min = std::min(along_first, along_second);
    output.along_max = std::max(along_first, along_second);
    if (output.along_min == output.along_max) return false;
    if (along_first < along_second) {
        output.height_at_min = first_z;
        output.height_at_max = second_z;
    } else {
        output.height_at_min = second_z;
        output.height_at_max = first_z;
    }
    output.seam = (static_cast<uint64_t>(std::min(owner, neighbor)) << 32)
        | static_cast<uint64_t>(std::max(owner, neighbor));
    output.source = source;
    output.owner = owner;
    return true;
}

double boundary_height_at(const BoundaryEdge& edge, int64_t along)
{
    const double extent = static_cast<double>(edge.along_max - edge.along_min);
    const double position = static_cast<double>(along - edge.along_min) / extent;
    return static_cast<double>(edge.height_at_min)
        + static_cast<double>(edge.height_at_max - edge.height_at_min) * position;
}

bool boundary_edges_within_climb(
    const BoundaryEdge& first,
    const BoundaryEdge& second)
{
    const int64_t overlap_min = std::max(first.along_min, second.along_min);
    const int64_t overlap_max = std::min(first.along_max, second.along_max);
    if (overlap_min >= overlap_max) return false;

    const double difference_at_min = boundary_height_at(first, overlap_min)
        - boundary_height_at(second, overlap_min);
    const double difference_at_max = boundary_height_at(first, overlap_max)
        - boundary_height_at(second, overlap_max);
    if (difference_at_min * difference_at_max <= 0.0) return true;
    return std::min(std::abs(difference_at_min), std::abs(difference_at_max))
        <= static_cast<double>(k_walkable_climb_quantized);
}

Vector<unsigned short> build_external_portals(
    const NavGeometry& geometry,
    std::span<const uint8_t> accepted,
    uint32_t area_width,
    uint32_t area_height)
{
    Vector<unsigned short> portals(geometry.indices.size(), k_mesh_null_index);
    Vector<BoundaryEdge> boundaries;

    for (size_t source = 0; source < geometry.indices.size(); ++source) {
        const size_t triangle = source / 3;
        if (!accepted[triangle]) continue;
        const int32_t neighbor = geometry.adjacency[source];
        if (neighbor >= 0 && static_cast<size_t>(neighbor) < accepted.size()
            && accepted[static_cast<size_t>(neighbor)]) {
            const uint32_t neighbor_owner = geometry.owner[static_cast<size_t>(neighbor)];
            if (neighbor_owner != geometry.owner[triangle]) {
                portals[source] = portal_direction(
                    geometry.owner[triangle], neighbor_owner, area_width);
            }
            continue;
        }

        BoundaryEdge boundary;
        if (make_boundary_edge(geometry, source, area_width, area_height, boundary)) {
            boundaries.push_back(boundary);
        }
    }

    std::sort(boundaries.begin(), boundaries.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.seam != rhs.seam) return lhs.seam < rhs.seam;
        if (lhs.along_min != rhs.along_min) return lhs.along_min < rhs.along_min;
        if (lhs.along_max != rhs.along_max) return lhs.along_max < rhs.along_max;
        return lhs.owner < rhs.owner;
    });

    for (size_t begin = 0; begin < boundaries.size();) {
        size_t end = begin + 1;
        while (end < boundaries.size() && boundaries[end].seam == boundaries[begin].seam)
            ++end;
        for (size_t first = begin; first < end; ++first) {
            for (size_t second = first + 1;
                second < end && boundaries[second].along_min < boundaries[first].along_max;
                ++second) {
                if (boundaries[first].owner == boundaries[second].owner
                    || boundaries[first].along_min >= boundaries[second].along_max
                    || !boundary_edges_within_climb(boundaries[first], boundaries[second])) {
                    continue;
                }
                portals[boundaries[first].source]
                    = static_cast<unsigned short>(k_portal_mask | boundaries[first].side);
                portals[boundaries[second].source]
                    = static_cast<unsigned short>(k_portal_mask | boundaries[second].side);
            }
        }
        begin = end;
    }
    return portals;
}

bool quantize_axis(float value, float origin, float step, unsigned short& output)
{
    const double quantized = std::round(
        (static_cast<double>(value) - static_cast<double>(origin)) / static_cast<double>(step));
    if (!std::isfinite(quantized) || quantized < 0.0
        || quantized > static_cast<double>(std::numeric_limits<unsigned short>::max())) {
        return false;
    }
    output = static_cast<unsigned short>(quantized);
    return true;
}

float signed_horizontal_area(
    const NavGeometry& geometry,
    uint32_t triangle)
{
    const size_t offset = static_cast<size_t>(triangle) * 3;
    const auto& a = geometry.vertices[geometry.indices[offset]];
    const auto& b = geometry.vertices[geometry.indices[offset + 1]];
    const auto& c = geometry.vertices[geometry.indices[offset + 2]];
    return (b.x - a.x) * (c.y - a.y)
        - (b.y - a.y) * (c.x - a.x);
}

struct DetourNavMeshDeleter {
    void operator()(dtNavMesh* value) const { dtFreeNavMesh(value); }
};

struct DetourQueryDeleter {
    void operator()(dtNavMeshQuery* value) const { dtFreeNavMeshQuery(value); }
};

} // namespace

struct NavWorldState::Impl {
    struct AgentState {
        dtPolyRef polygon = 0;
        float clearance = 0.0f;
        uint8_t active = 0;
    };

    std::unique_ptr<dtNavMesh, DetourNavMeshDeleter> mesh;
    std::unique_ptr<dtNavMeshQuery, DetourQueryDeleter> query;
    dtQueryFilter filter;
    Vector<dtPolyRef> global_polygon_refs;
    Vector<unsigned short> base_polygon_flags;
    Vector<uint32_t> polygon_blocker_counts;
    Vector<uint32_t> blocker_owners;
    Vector<uint32_t> blocker_owner_offsets;
    Vector<uint32_t> blocker_triangles;
    Vector<uint8_t> blocker_closed;
    // Movement consumes these fields together through a caller-supplied agent
    // index. Keep each agent on one row instead of touching three independent
    // arrays for every random-indexed input.
    Vector<AgentState> agents;
};

namespace {

bool ray_segment_intersects_bounds(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& displacement,
    const float* minimum,
    const float* maximum,
    float maximum_fraction) noexcept
{
    constexpr float k_direction_epsilon = 1.0e-8f;
    float near_fraction = 0.0f;
    float far_fraction = maximum_fraction;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(displacement[axis]) <= k_direction_epsilon) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
            continue;
        }
        const float inverse = 1.0f / displacement[axis];
        float axis_near = (minimum[axis] - origin[axis]) * inverse;
        float axis_far = (maximum[axis] - origin[axis]) * inverse;
        if (axis_near > axis_far) std::swap(axis_near, axis_far);
        near_fraction = std::max(near_fraction, axis_near);
        far_fraction = std::min(far_fraction, axis_far);
        if (far_fraction < near_fraction) return false;
    }
    return true;
}

bool ray_segment_intersects_triangle(
    const glm::vec3& origin,
    const glm::vec3& displacement,
    const glm::vec3& first,
    const glm::vec3& second,
    const glm::vec3& third,
    float maximum_fraction,
    float& fraction) noexcept
{
    constexpr float k_determinant_epsilon = 1.0e-7f;
    const glm::vec3 edge0 = second - first;
    const glm::vec3 edge1 = third - first;
    const glm::vec3 perpendicular = glm::cross(displacement, edge1);
    const float determinant = glm::dot(edge0, perpendicular);
    if (!std::isfinite(determinant) || std::abs(determinant) <= k_determinant_epsilon) {
        return false;
    }

    const float inverse_determinant = 1.0f / determinant;
    const glm::vec3 origin_delta = origin - first;
    const float u = glm::dot(origin_delta, perpendicular) * inverse_determinant;
    if (!std::isfinite(u) || u < 0.0f || u > 1.0f) return false;

    const glm::vec3 cross = glm::cross(origin_delta, edge0);
    const float v = glm::dot(displacement, cross) * inverse_determinant;
    if (!std::isfinite(v) || v < 0.0f || u + v > 1.0f) return false;

    const float hit_fraction = glm::dot(edge1, cross) * inverse_determinant;
    if (!std::isfinite(hit_fraction) || hit_fraction < 0.0f
        || hit_fraction > maximum_fraction) {
        return false;
    }
    fraction = hit_fraction;
    return true;
}

bool constrain_agent_clearance(
    NavWorldState::Impl& impl,
    dtPolyRef& polygon,
    std::array<float, 3>& position,
    float clearance,
    bool& adjusted)
{
    adjusted = false;
    if (clearance <= 0.0f) return true;

    for (size_t iteration = 0; iteration < k_max_clearance_iterations; ++iteration) {
        float wall_distance = clearance;
        std::array<float, 3> wall_position{};
        std::array<float, 3> wall_normal{};
        const dtStatus wall_status = impl.query->findDistanceToWall(
            polygon, position.data(), clearance, &impl.filter,
            &wall_distance, wall_position.data(), wall_normal.data());
        if (dtStatusFailed(wall_status)) return false;
        if (wall_distance + k_clearance_epsilon >= clearance) return true;

        const float horizontal_normal_squared = wall_normal[0] * wall_normal[0]
            + wall_normal[2] * wall_normal[2];
        if (!std::isfinite(horizontal_normal_squared)
            || horizontal_normal_squared <= k_clearance_epsilon * k_clearance_epsilon) {
            return false;
        }

        const float inverse_normal_length = 1.0f / std::sqrt(horizontal_normal_squared);
        const float correction = clearance - wall_distance + k_clearance_epsilon;
        const std::array<float, 3> desired{
            position[0] + wall_normal[0] * inverse_normal_length * correction,
            position[1],
            position[2] + wall_normal[2] * inverse_normal_length * correction,
        };
        std::array<float, 3> resolved{};
        std::array<dtPolyRef, k_max_move_polygons> visited{};
        int visited_count = 0;
        const dtStatus move_status = impl.query->moveAlongSurface(
            polygon, position.data(), desired.data(), &impl.filter,
            resolved.data(), visited.data(), &visited_count, static_cast<int>(visited.size()));
        if (dtStatusFailed(move_status) || visited_count == 0) return false;
        polygon = visited[static_cast<size_t>(visited_count - 1)];
        position = resolved;
        adjusted = true;
    }

    float wall_distance = clearance;
    std::array<float, 3> wall_position{};
    std::array<float, 3> wall_normal{};
    const dtStatus status = impl.query->findDistanceToWall(
        polygon, position.data(), clearance, &impl.filter,
        &wall_distance, wall_position.data(), wall_normal.data());
    return dtStatusSucceed(status) && wall_distance + k_clearance_epsilon >= clearance;
}

bool retreat_from_polygon_boundary(
    const NavWorldState::Impl& impl,
    dtPolyRef polygon,
    std::array<float, 3>& position,
    float clearance)
{
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    if (dtStatusFailed(impl.mesh->getTileAndPolyByRef(polygon, &tile, &poly))
        || !tile || !poly || poly->vertCount == 0) {
        return false;
    }

    std::array<float, 3> center{};
    for (unsigned int vertex = 0; vertex < poly->vertCount; ++vertex) {
        const float* source = &tile->verts[poly->verts[vertex] * 3];
        center[0] += source[0];
        center[1] += source[1];
        center[2] += source[2];
    }
    const float inverse_count = 1.0f / static_cast<float>(poly->vertCount);
    center[0] *= inverse_count;
    center[1] *= inverse_count;
    center[2] *= inverse_count;

    const float delta_x = center[0] - position[0];
    const float delta_z = center[2] - position[2];
    const float distance = std::sqrt(delta_x * delta_x + delta_z * delta_z);
    if (distance <= k_clearance_epsilon) return false;

    const float retreat = std::min(clearance + k_clearance_epsilon, distance);
    position[0] += delta_x / distance * retreat;
    position[2] += delta_z / distance * retreat;
    return true;
}

bool nudge_funnel_corner(
    const float* previous,
    std::array<float, 3>& corner,
    const float* next)
{
    const float incoming_x = corner[0] - previous[0];
    const float incoming_z = corner[2] - previous[2];
    const float outgoing_x = next[0] - corner[0];
    const float outgoing_z = next[2] - corner[2];
    const float incoming_length = std::sqrt(incoming_x * incoming_x + incoming_z * incoming_z);
    const float outgoing_length = std::sqrt(outgoing_x * outgoing_x + outgoing_z * outgoing_z);
    if (incoming_length <= k_clearance_epsilon || outgoing_length <= k_clearance_epsilon) return false;

    const float in_x = incoming_x / incoming_length;
    const float in_z = incoming_z / incoming_length;
    const float out_x = outgoing_x / outgoing_length;
    const float out_z = outgoing_z / outgoing_length;
    const float turn = in_x * out_z - in_z * out_x;
    if (std::abs(turn) <= k_clearance_epsilon) return false;

    const float side = turn > 0.0f ? 1.0f : -1.0f;
    const float away_x = side * (in_z + out_z);
    const float away_z = -side * (in_x + out_x);
    const float away_length = std::sqrt(away_x * away_x + away_z * away_z);
    if (away_length <= k_clearance_epsilon) return false;

    corner[0] += away_x / away_length * k_quantization_step;
    corner[2] += away_z / away_length * k_quantization_step;
    return true;
}

void resolve_agent_height(
    NavWorldState::Impl& impl, dtPolyRef polygon, std::array<float, 3>& position)
{
    float height = position[1];
    if (dtStatusSucceed(impl.query->getPolyHeight(polygon, position.data(), &height))) {
        position[1] = height;
    }
}

} // namespace

NavWorldState::NavWorldState() = default;
NavWorldState::~NavWorldState() = default;
NavWorldState::NavWorldState(NavWorldState&&) noexcept = default;
NavWorldState& NavWorldState::operator=(NavWorldState&&) noexcept = default;

NavStatus build_nav_world(
    const NavGeometry& geometry,
    std::span<const uint8_t> surface_walkable,
    uint32_t area_width,
    uint32_t area_height,
    NavWorldState& world,
    NavBuildStats& stats)
{
    stats = {};
    stats.input_triangle_count = geometry.triangle_count();
    world.impl.reset();
    if (!geometry.valid() || geometry.adjacency.size() != geometry.indices.size()
        || stats.input_triangle_count > std::numeric_limits<uint32_t>::max()
        || area_width == 0 || area_height == 0
        || static_cast<uint64_t>(area_width) * area_height > std::numeric_limits<uint32_t>::max()) {
        return NavStatus::rejected;
    }

    const uint32_t declared_tiles = area_width * area_height;
    Vector<uint32_t> tile_counts(declared_tiles, 0);
    Vector<uint8_t> accepted(stats.input_triangle_count, 0);
    for (size_t triangle = 0; triangle < stats.input_triangle_count; ++triangle) {
        const uint32_t owner = geometry.owner[triangle];
        const uint32_t surface = geometry.surface[triangle];
        if (owner >= declared_tiles || surface >= surface_walkable.size()
            || surface_walkable[surface] == 0) {
            ++stats.rejected_triangle_count;
            continue;
        }
        if (tile_counts[owner] == std::numeric_limits<uint32_t>::max()) return NavStatus::rejected;
        ++tile_counts[owner];
        accepted[triangle] = 1;
        ++stats.walkable_triangle_count;
    }
    if (stats.walkable_triangle_count == 0) return NavStatus::off_mesh;

    Vector<uint32_t> tile_offsets(static_cast<size_t>(declared_tiles) + 1, 0);
    uint32_t max_polygons = 0;
    for (uint32_t tile = 0; tile < declared_tiles; ++tile) {
        if (tile_counts[tile] > std::numeric_limits<unsigned short>::max()) return NavStatus::rejected;
        tile_offsets[tile + 1] = tile_offsets[tile] + tile_counts[tile];
        max_polygons = std::max(max_polygons, tile_counts[tile]);
        stats.tile_count += static_cast<size_t>(tile_counts[tile] != 0);
    }
    Vector<uint32_t> tile_triangles(stats.walkable_triangle_count);
    Vector<uint32_t> cursors = tile_offsets;
    for (size_t triangle = 0; triangle < accepted.size(); ++triangle) {
        if (!accepted[triangle]) continue;
        tile_triangles[cursors[geometry.owner[triangle]]++] = static_cast<uint32_t>(triangle);
    }
    const Vector<unsigned short> external_portals = build_external_portals(
        geometry, accepted, area_width, area_height);

    auto impl = std::make_unique<NavWorldState::Impl>();
    impl->mesh.reset(dtAllocNavMesh());
    if (!impl->mesh) return NavStatus::rejected;
    dtNavMeshParams mesh_params{};
    mesh_params.orig[0] = 0.0f;
    mesh_params.orig[1] = 0.0f;
    mesh_params.orig[2] = 0.0f;
    mesh_params.tileWidth = k_tile_size;
    mesh_params.tileHeight = k_tile_size;
    if (stats.tile_count > static_cast<size_t>(std::numeric_limits<int>::max())
        || max_polygons > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return NavStatus::rejected;
    }
    mesh_params.maxTiles = static_cast<int>(stats.tile_count);
    mesh_params.maxPolys = static_cast<int>(max_polygons);
    if (dtStatusFailed(impl->mesh->init(&mesh_params))) return NavStatus::rejected;

    impl->global_polygon_refs.assign(stats.input_triangle_count, 0);
    impl->base_polygon_flags.assign(stats.input_triangle_count, 0);
    impl->polygon_blocker_counts.assign(stats.input_triangle_count, 0);
    Vector<uint32_t> vertex_remap(geometry.vertices.size(), UINT32_MAX);
    Vector<uint32_t> triangle_remap(stats.input_triangle_count, UINT32_MAX);
    Vector<uint32_t> touched_vertices;
    Vector<unsigned short> tile_vertices;
    Vector<unsigned short> tile_polygons;
    Vector<unsigned short> tile_flags;
    Vector<unsigned char> tile_areas;

    for (uint32_t tile = 0; tile < declared_tiles; ++tile) {
        const uint32_t begin = tile_offsets[tile];
        const uint32_t end = tile_offsets[tile + 1];
        if (begin == end) continue;

        touched_vertices.clear();
        for (uint32_t row = begin; row < end; ++row) {
            const uint32_t triangle = tile_triangles[row];
            for (uint32_t edge = 0; edge < 3; ++edge) {
                const uint32_t vertex = geometry.indices[static_cast<size_t>(triangle) * 3 + edge];
                if (vertex >= geometry.vertices.size() || !finite(geometry.vertices[vertex])) {
                    ++stats.rejected_tile_count;
                    return NavStatus::rejected;
                }
                if (vertex_remap[vertex] == UINT32_MAX) {
                    vertex_remap[vertex] = static_cast<uint32_t>(touched_vertices.size());
                    touched_vertices.push_back(vertex);
                }
            }
        }
        if (touched_vertices.size() > std::numeric_limits<unsigned short>::max()) {
            ++stats.rejected_tile_count;
            return NavStatus::rejected;
        }

        std::array<float, 3> minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        std::array<float, 3> maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
        };
        for (uint32_t vertex : touched_vertices) {
            const auto detour = to_detour(geometry.vertices[vertex]);
            for (size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], detour[axis]);
                maximum[axis] = std::max(maximum[axis], detour[axis]);
            }
        }
        for (size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::floor(minimum[axis] / k_quantization_step) * k_quantization_step;
            maximum[axis] = std::ceil(maximum[axis] / k_quantization_step) * k_quantization_step;
        }
        // All NWN area tiles occupy the same 10 m world grid. Keep Detour's
        // horizontal quantization origin independent of each model's bounds so
        // neighboring tiles encode a shared edge in the same coordinate frame.
        minimum[0] = static_cast<float>(tile % area_width) * k_tile_size;
        minimum[2] = static_cast<float>(tile / area_width) * k_tile_size;
        maximum[0] = minimum[0] + k_tile_size;
        maximum[2] = minimum[2] + k_tile_size;

        tile_vertices.assign(touched_vertices.size() * 3, 0);
        for (size_t local = 0; local < touched_vertices.size(); ++local) {
            const auto detour = to_detour(geometry.vertices[touched_vertices[local]]);
            for (size_t axis = 0; axis < 3; ++axis) {
                if (!quantize_axis(detour[axis], minimum[axis], k_quantization_step,
                        tile_vertices[local * 3 + axis])) {
                    ++stats.rejected_tile_count;
                    return NavStatus::rejected;
                }
            }
        }

        const size_t polygon_count = end - begin;
        tile_polygons.assign(polygon_count * 6, k_mesh_null_index);
        tile_flags.assign(polygon_count, k_walkable_flag);
        tile_areas.assign(polygon_count, 0);
        for (uint32_t row = begin; row < end; ++row) {
            triangle_remap[tile_triangles[row]] = row - begin;
        }
        for (uint32_t row = begin; row < end; ++row) {
            const uint32_t triangle = tile_triangles[row];
            const size_t local = row - begin;
            // Detour derives portal left/right from clockwise polygon winding.
            // NWN walkmeshes are predominantly counter-clockwise, and custom
            // content may mix both. Normalize only the cold-build output while
            // preserving each adjacency entry's association with its source edge.
            constexpr std::array<uint32_t, 3> forward_vertices{0, 1, 2};
            constexpr std::array<uint32_t, 3> forward_edges{0, 1, 2};
            constexpr std::array<uint32_t, 3> reversed_vertices{0, 2, 1};
            constexpr std::array<uint32_t, 3> reversed_edges{2, 1, 0};
            const bool reverse = signed_horizontal_area(geometry, triangle) > 0.0f;
            const auto& vertex_offsets = reverse ? reversed_vertices : forward_vertices;
            const auto& edge_offsets = reverse ? reversed_edges : forward_edges;
            for (uint32_t edge = 0; edge < 3; ++edge) {
                const size_t vertex_source = static_cast<size_t>(triangle) * 3
                    + vertex_offsets[edge];
                const size_t edge_source = static_cast<size_t>(triangle) * 3
                    + edge_offsets[edge];
                tile_polygons[local * 6 + edge]
                    = static_cast<unsigned short>(vertex_remap[geometry.indices[vertex_source]]);
                const int32_t neighbor = geometry.adjacency[edge_source];
                if (neighbor >= 0 && static_cast<size_t>(neighbor) < accepted.size()
                    && accepted[static_cast<size_t>(neighbor)]
                    && geometry.owner[static_cast<size_t>(neighbor)] == tile) {
                    tile_polygons[local * 6 + 3 + edge]
                        = static_cast<unsigned short>(triangle_remap[static_cast<size_t>(neighbor)]);
                } else if (external_portals[edge_source] != k_mesh_null_index) {
                    tile_polygons[local * 6 + 3 + edge] = external_portals[edge_source];
                    ++stats.portal_edge_count;
                }
            }
        }

        dtNavMeshCreateParams params{};
        params.verts = tile_vertices.data();
        params.vertCount = static_cast<int>(touched_vertices.size());
        params.polys = tile_polygons.data();
        params.polyFlags = tile_flags.data();
        params.polyAreas = tile_areas.data();
        params.polyCount = static_cast<int>(polygon_count);
        params.nvp = 3;
        params.tileX = static_cast<int>(tile % area_width);
        params.tileY = static_cast<int>(tile / area_width);
        params.tileLayer = 0;
        std::memcpy(params.bmin, minimum.data(), sizeof(params.bmin));
        std::memcpy(params.bmax, maximum.data(), sizeof(params.bmax));
        params.walkableHeight = k_walkable_height;
        params.walkableRadius = k_walkable_radius;
        params.walkableClimb = k_walkable_climb;
        params.cs = k_quantization_step;
        params.ch = k_quantization_step;
        params.buildBvTree = true;

        unsigned char* payload = nullptr;
        int payload_size = 0;
        if (!dtCreateNavMeshData(&params, &payload, &payload_size) || !payload) {
            ++stats.rejected_tile_count;
            return NavStatus::rejected;
        }
        dtTileRef tile_ref = 0;
        const dtStatus add_status = impl->mesh->addTile(
            payload, payload_size, DT_TILE_FREE_DATA, 0, &tile_ref);
        if (dtStatusFailed(add_status)) {
            dtFree(payload);
            ++stats.rejected_tile_count;
            return NavStatus::rejected;
        }
        const dtPolyRef base_ref = impl->mesh->getPolyRefBase(impl->mesh->getTileByRef(tile_ref));
        for (uint32_t row = begin; row < end; ++row) {
            const uint32_t triangle = tile_triangles[row];
            impl->global_polygon_refs[triangle] = base_ref | (row - begin);
            impl->base_polygon_flags[triangle] = k_walkable_flag;
        }
        stats.vertex_count += touched_vertices.size();
        stats.polygon_count += polygon_count;
        stats.payload_bytes += static_cast<size_t>(payload_size);
        for (uint32_t vertex : touched_vertices)
            vertex_remap[vertex] = UINT32_MAX;
        for (uint32_t row = begin; row < end; ++row)
            triangle_remap[tile_triangles[row]] = UINT32_MAX;
    }

    impl->query.reset(dtAllocNavMeshQuery());
    if (!impl->query || dtStatusFailed(impl->query->init(impl->mesh.get(), k_query_nodes))) {
        return NavStatus::rejected;
    }
    impl->filter.setIncludeFlags(k_walkable_flag);
    world.impl = std::move(impl);
    return NavStatus::ok;
}

NavBatchStats collect_nav_debug_triangles(
    const NavWorldState& world,
    Vector<NavDebugTriangle>& triangles)
{
    NavBatchStats stats;
    triangles.clear();
    if (!world.impl || !world.impl->mesh) {
        stats.rejected_count = 1;
        return stats;
    }

    const auto& impl = *world.impl;
    const dtNavMesh& mesh = *impl.mesh;
    for (int tile_index = 0; tile_index < mesh.getMaxTiles(); ++tile_index) {
        const dtMeshTile* tile = mesh.getTile(tile_index);
        if (!tile || !tile->header || !tile->verts || !tile->polys) continue;

        stats.input_count += static_cast<size_t>(tile->header->polyCount);
        for (int polygon_index = 0;
            polygon_index < tile->header->polyCount;
            ++polygon_index) {
            const dtPoly& polygon = tile->polys[polygon_index];
            if (polygon.getType() == DT_POLYTYPE_OFFMESH_CONNECTION
                || polygon.vertCount < 3) {
                ++stats.rejected_count;
                continue;
            }

            const glm::vec3 first = from_detour(
                &tile->verts[static_cast<size_t>(polygon.verts[0]) * 3]);
            if (!finite(first)) {
                ++stats.rejected_count;
                continue;
            }
            const auto state = polygon.flags == 0
                ? NavDebugPolygonState::blocked
                : NavDebugPolygonState::walkable;
            for (unsigned int vertex = 1; vertex + 1 < polygon.vertCount; ++vertex) {
                const glm::vec3 second = from_detour(
                    &tile->verts[static_cast<size_t>(polygon.verts[vertex]) * 3]);
                const glm::vec3 third = from_detour(
                    &tile->verts[static_cast<size_t>(polygon.verts[vertex + 1]) * 3]);
                if (!finite(second) || !finite(third)) {
                    ++stats.rejected_count;
                    continue;
                }
                triangles.push_back({first, second, third, state});
                ++stats.output_count;
            }
        }
    }
    return stats;
}

NavBatchStats project_nav_rays(
    const NavWorldState& world,
    std::span<const NavRayProjectionInput> inputs,
    std::span<NavRayProjectionResult> results)
{
    NavBatchStats stats;
    stats.input_count = inputs.size();
    if (results.size() < inputs.size()) {
        stats.rejected_count = inputs.size();
        return stats;
    }

    for (size_t input_index = 0; input_index < inputs.size(); ++input_index) {
        const auto& input = inputs[input_index];
        auto& result = results[input_index];
        result = {};
        if (!world.impl || !world.impl->mesh
            || !finite(input.origin) || !finite(input.displacement)
            || glm::length2(input.displacement) <= 1.0e-12f) {
            ++stats.rejected_count;
            continue;
        }

        result.status = NavStatus::off_mesh;
        float nearest_fraction = 1.0f;
        bool found = false;
        const auto detour_origin = to_detour(input.origin);
        const auto detour_displacement = to_detour(input.displacement);
        const dtNavMesh& mesh = *world.impl->mesh;
        const int tile_count = mesh.getMaxTiles();
        for (int tile_index = 0; tile_index < tile_count; ++tile_index) {
            const dtMeshTile* tile = mesh.getTile(tile_index);
            if (!tile || !tile->header || !tile->verts || !tile->polys
                || !ray_segment_intersects_bounds(detour_origin, detour_displacement,
                    tile->header->bmin, tile->header->bmax, nearest_fraction)) {
                continue;
            }

            for (int polygon_index = 0;
                polygon_index < tile->header->polyCount;
                ++polygon_index) {
                const dtPoly& polygon = tile->polys[polygon_index];
                if (polygon.getType() == DT_POLYTYPE_OFFMESH_CONNECTION
                    || polygon.vertCount < 3
                    || (polygon.flags & world.impl->filter.getIncludeFlags()) == 0
                    || (polygon.flags & world.impl->filter.getExcludeFlags()) != 0) {
                    continue;
                }

                const glm::vec3 first = from_detour(
                    &tile->verts[static_cast<size_t>(polygon.verts[0]) * 3]);
                for (unsigned int vertex = 1; vertex + 1 < polygon.vertCount; ++vertex) {
                    const glm::vec3 second = from_detour(
                        &tile->verts[static_cast<size_t>(polygon.verts[vertex]) * 3]);
                    const glm::vec3 third = from_detour(
                        &tile->verts[static_cast<size_t>(polygon.verts[vertex + 1]) * 3]);
                    float fraction = 0.0f;
                    if (!ray_segment_intersects_triangle(input.origin,
                            input.displacement, first, second, third,
                            nearest_fraction, fraction)) {
                        continue;
                    }
                    nearest_fraction = fraction;
                    found = true;
                }
            }
        }

        if (!found) {
            ++stats.blocked_count;
            continue;
        }
        result.position = input.origin + input.displacement * nearest_fraction;
        result.fraction = nearest_fraction;
        result.status = NavStatus::ok;
        ++stats.output_count;
    }
    return stats;
}

NavBlockerBuildStats configure_nav_blockers(
    NavWorldState& world,
    std::span<const NavBlockerOverlapInput> overlaps)
{
    NavBlockerBuildStats stats;
    stats.input_count = overlaps.size();
    if (!world.impl || !world.impl->mesh
        || overlaps.size() > std::numeric_limits<uint32_t>::max()) {
        stats.rejected_count = overlaps.size();
        return stats;
    }

    auto& impl = *world.impl;
    for (size_t triangle = 0; triangle < impl.polygon_blocker_counts.size(); ++triangle) {
        if (impl.polygon_blocker_counts[triangle] == 0 || impl.global_polygon_refs[triangle] == 0) continue;
        impl.mesh->setPolyFlags(impl.global_polygon_refs[triangle], impl.base_polygon_flags[triangle]);
    }
    std::fill(impl.polygon_blocker_counts.begin(), impl.polygon_blocker_counts.end(), 0);
    impl.blocker_owners.clear();
    impl.blocker_owner_offsets.clear();
    impl.blocker_triangles.clear();
    impl.blocker_closed.clear();

    Vector<NavBlockerOverlapInput> accepted;
    accepted.reserve(overlaps.size());
    for (const auto& overlap : overlaps) {
        if (overlap.triangle >= impl.global_polygon_refs.size()
            || impl.global_polygon_refs[overlap.triangle] == 0) {
            ++stats.rejected_count;
            continue;
        }
        accepted.push_back(overlap);
    }
    std::sort(accepted.begin(), accepted.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.owner != rhs.owner) return lhs.owner < rhs.owner;
        return lhs.triangle < rhs.triangle;
    });
    const auto unique_end = std::unique(accepted.begin(), accepted.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.owner == rhs.owner && lhs.triangle == rhs.triangle;
    });
    stats.duplicate_count = static_cast<size_t>(accepted.end() - unique_end);
    accepted.erase(unique_end, accepted.end());

    impl.blocker_owner_offsets.push_back(0);
    for (const auto& overlap : accepted) {
        if (impl.blocker_owners.empty() || impl.blocker_owners.back() != overlap.owner) {
            if (!impl.blocker_owners.empty()) {
                impl.blocker_owner_offsets.push_back(
                    static_cast<uint32_t>(impl.blocker_triangles.size()));
            }
            impl.blocker_owners.push_back(overlap.owner);
            impl.blocker_closed.push_back(1);
        }
        impl.blocker_triangles.push_back(overlap.triangle);
        uint32_t& count = impl.polygon_blocker_counts[overlap.triangle];
        if (count == 0) {
            impl.mesh->setPolyFlags(impl.global_polygon_refs[overlap.triangle], 0);
        }
        ++count;
    }
    if (!impl.blocker_owners.empty()) {
        impl.blocker_owner_offsets.push_back(
            static_cast<uint32_t>(impl.blocker_triangles.size()));
    }
    stats.owner_count = impl.blocker_owners.size();
    stats.overlap_count = impl.blocker_triangles.size();
    return stats;
}

NavBatchStats set_nav_blockers_closed(
    NavWorldState& world,
    std::span<const NavBlockerStateInput> inputs)
{
    NavBatchStats stats;
    stats.input_count = inputs.size();
    if (!world.impl || !world.impl->mesh) {
        stats.rejected_count = inputs.size();
        return stats;
    }

    auto& impl = *world.impl;
    for (const auto& input : inputs) {
        const auto found = std::lower_bound(
            impl.blocker_owners.begin(), impl.blocker_owners.end(), input.owner);
        if (found == impl.blocker_owners.end() || *found != input.owner) {
            ++stats.rejected_count;
            continue;
        }
        const size_t owner_index = static_cast<size_t>(found - impl.blocker_owners.begin());
        const uint8_t closed = static_cast<uint8_t>(input.closed);
        if (impl.blocker_closed[owner_index] == closed) {
            ++stats.output_count;
            continue;
        }

        const uint32_t begin = impl.blocker_owner_offsets[owner_index];
        const uint32_t end = impl.blocker_owner_offsets[owner_index + 1];
        for (uint32_t row = begin; row < end; ++row) {
            const uint32_t triangle = impl.blocker_triangles[row];
            uint32_t& count = impl.polygon_blocker_counts[triangle];
            if (input.closed) {
                if (count == 0) {
                    impl.mesh->setPolyFlags(impl.global_polygon_refs[triangle], 0);
                }
                ++count;
            } else {
                if (count == 0) continue;
                --count;
                if (count == 0) {
                    impl.mesh->setPolyFlags(
                        impl.global_polygon_refs[triangle], impl.base_polygon_flags[triangle]);
                }
            }
        }
        impl.blocker_closed[owner_index] = closed;
        ++stats.output_count;
    }
    return stats;
}

NavBatchStats find_nav_paths(
    const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena,
    std::span<NavPathResult> results)
{
    NavBatchStats stats;
    stats.input_count = requests.size();
    corner_arena.clear();
    const size_t count = std::min(requests.size(), results.size());
    if (!world.impl || !world.impl->query) {
        for (size_t i = 0; i < count; ++i)
            results[i] = {};
        stats.rejected_count = requests.size();
        return stats;
    }

    std::array<dtPolyRef, k_max_path_polygons> path;
    std::array<float, k_max_path_corners * 3> corners;
    std::array<dtPolyRef, k_max_path_corners> corner_polygons;
    for (size_t i = 0; i < count; ++i) {
        results[i] = {};
        if (!finite(requests[i].start) || !finite(requests[i].end)
            || !std::isfinite(requests[i].clearance)
            || requests[i].clearance < 0.0f) {
            ++stats.rejected_count;
            continue;
        }
        const auto start = to_detour(requests[i].start);
        const auto end = to_detour(requests[i].end);
        std::array<float, 3> nearest_start{};
        std::array<float, 3> nearest_end{};
        dtPolyRef start_ref = 0;
        if (dtStatusFailed(world.impl->query->findNearestPoly(start.data(), k_nearest_half_extents.data(),
                &world.impl->filter, &start_ref, nearest_start.data()))
            || start_ref == 0) {
            results[i].status = NavStatus::off_mesh;
            ++stats.rejected_count;
            continue;
        }

        dtPolyRef end_ref = 0;
        int path_count = 0;
        dtStatus path_status = DT_SUCCESS;
        bool partial_path = false;
        bool has_endpoint_normal = false;
        std::array<float, 3> endpoint_normal{};
        const dtStatus nearest_end_status = world.impl->query->findNearestPoly(
            end.data(), k_nearest_half_extents.data(), &world.impl->filter,
            &end_ref, nearest_end.data());
        const bool endpoint_resolved = dtStatusSucceed(nearest_end_status) && end_ref != 0;
        if (endpoint_resolved) {
            const float endpoint_delta_x = nearest_end[0] - end[0];
            const float endpoint_delta_z = nearest_end[2] - end[2];
            partial_path = endpoint_delta_x * endpoint_delta_x
                    + endpoint_delta_z * endpoint_delta_z
                > k_clearance_epsilon * k_clearance_epsilon;
        }
        if (endpoint_resolved && !partial_path) {
            path_status = world.impl->query->findPath(
                start_ref, end_ref, nearest_start.data(), nearest_end.data(), &world.impl->filter,
                path.data(), &path_count, static_cast<int>(path.size()));
        } else {
            float hit_parameter = 0.0f;
            path_status = world.impl->query->raycast(
                start_ref, nearest_start.data(), end.data(), &world.impl->filter,
                &hit_parameter, endpoint_normal.data(), path.data(), &path_count,
                static_cast<int>(path.size()));
            if (dtStatusSucceed(path_status) && path_count > 0
                && hit_parameter > k_clearance_epsilon) {
                const float interpolation = std::min(hit_parameter, 1.0f);
                const std::array<float, 3> ray_endpoint{
                    nearest_start[0] + (end[0] - nearest_start[0]) * interpolation,
                    nearest_start[1] + (end[1] - nearest_start[1]) * interpolation,
                    nearest_start[2] + (end[2] - nearest_start[2]) * interpolation,
                };
                end_ref = path[static_cast<size_t>(path_count - 1)];
                path_status |= world.impl->query->closestPointOnPoly(
                    end_ref, ray_endpoint.data(), nearest_end.data(), nullptr);
                partial_path = true;
                const float normal_length_squared = endpoint_normal[0] * endpoint_normal[0]
                    + endpoint_normal[2] * endpoint_normal[2];
                has_endpoint_normal = hit_parameter < 1.0f
                    && normal_length_squared > k_clearance_epsilon * k_clearance_epsilon;
            } else {
                path_count = 0;
            }
        }
        if (dtStatusFailed(path_status) || path_count == 0) {
            results[i].status = NavStatus::blocked;
            ++stats.blocked_count;
            continue;
        }
        partial_path = partial_path
            || path[static_cast<size_t>(path_count - 1)] != end_ref
            || dtStatusDetail(path_status, DT_PARTIAL_RESULT);
        if (path[static_cast<size_t>(path_count - 1)] != end_ref
            || dtStatusDetail(path_status, DT_PARTIAL_RESULT)) {
            const dtStatus endpoint_status = world.impl->query->closestPointOnPoly(
                path[static_cast<size_t>(path_count - 1)], nearest_end.data(),
                nearest_end.data(), nullptr);
            if (dtStatusFailed(endpoint_status)) {
                ++stats.rejected_count;
                continue;
            }
        }
        int corner_count = 0;
        const dtStatus straight_status = world.impl->query->findStraightPath(
            nearest_start.data(), nearest_end.data(), path.data(), path_count,
            corners.data(), nullptr, corner_polygons.data(), &corner_count, k_max_path_corners);
        if (dtStatusFailed(straight_status)) {
            ++stats.rejected_count;
            continue;
        }

        bool adjusted_path = false;
        if (partial_path && requests[i].clearance > 0.0f && corner_count > 0) {
            const size_t endpoint = static_cast<size_t>(corner_count - 1) * 3;
            std::array<float, 3> adjusted_endpoint{
                corners[endpoint], corners[endpoint + 1], corners[endpoint + 2]};
            if (has_endpoint_normal) {
                const float normal_length = std::sqrt(
                    endpoint_normal[0] * endpoint_normal[0]
                    + endpoint_normal[2] * endpoint_normal[2]);
                const float retreat = requests[i].clearance + k_clearance_epsilon;
                adjusted_endpoint[0] += endpoint_normal[0] / normal_length * retreat;
                adjusted_endpoint[2] += endpoint_normal[2] / normal_length * retreat;
                adjusted_path = true;
            } else {
                adjusted_path = retreat_from_polygon_boundary(
                    *world.impl, path[static_cast<size_t>(path_count - 1)],
                    adjusted_endpoint, requests[i].clearance);
            }
            corners[endpoint] = adjusted_endpoint[0];
            corners[endpoint + 1] = adjusted_endpoint[1];
            corners[endpoint + 2] = adjusted_endpoint[2];
        }
        bool valid_path = true;
        std::array<float, 3> previous_raw_corner{};
        for (int corner = 0; corner < corner_count; ++corner) {
            const size_t corner_index = static_cast<size_t>(corner);
            dtPolyRef polygon = corner_polygons[corner_index];
            if (polygon == 0) {
                if (corner == 0)
                    polygon = start_ref;
                else if (corner + 1 == corner_count)
                    polygon = path[static_cast<size_t>(path_count - 1)];
                else {
                    valid_path = false;
                    break;
                }
            }

            const std::array<float, 3> raw_corner{
                corners[corner_index * 3],
                corners[corner_index * 3 + 1],
                corners[corner_index * 3 + 2],
            };
            std::array<float, 3> adjusted_corner = raw_corner;
            bool adjusted = requests[i].clearance > 0.0f
                && corner > 0
                && corner + 1 < corner_count
                && nudge_funnel_corner(
                    previous_raw_corner.data(), adjusted_corner,
                    &corners[(corner_index + 1) * 3]);
            bool clearance_adjusted = false;
            if (!constrain_agent_clearance(
                    *world.impl, polygon, adjusted_corner, requests[i].clearance, clearance_adjusted)) {
                valid_path = false;
                break;
            }
            resolve_agent_height(*world.impl, polygon, adjusted_corner);
            corners[corner_index * 3] = adjusted_corner[0];
            corners[corner_index * 3 + 1] = adjusted_corner[1];
            corners[corner_index * 3 + 2] = adjusted_corner[2];
            adjusted_path = adjusted_path || adjusted || clearance_adjusted;
            previous_raw_corner = raw_corner;
        }
        if (!valid_path) {
            results[i].status = NavStatus::blocked;
            ++stats.blocked_count;
            continue;
        }
        if (corner_arena.size() > UINT32_MAX - static_cast<size_t>(corner_count)) {
            results[i].status = NavStatus::output_full;
            ++stats.rejected_count;
            continue;
        }
        results[i].corner_offset = static_cast<uint32_t>(corner_arena.size());
        results[i].corner_count = static_cast<uint32_t>(corner_count);
        results[i].status = partial_path
                || dtStatusDetail(path_status, DT_BUFFER_TOO_SMALL)
                || dtStatusDetail(straight_status, DT_BUFFER_TOO_SMALL)
                || adjusted_path
            ? NavStatus::clamped
            : NavStatus::ok;
        for (int corner = 0; corner < corner_count; ++corner) {
            corner_arena.push_back(from_detour(&corners[static_cast<size_t>(corner) * 3]));
        }
        ++stats.output_count;
        stats.clamped_count += static_cast<size_t>(results[i].status == NavStatus::clamped);
    }
    stats.rejected_count += requests.size() - count;
    return stats;
}

NavBatchStats register_nav_agents(
    NavWorldState& world,
    std::span<const NavAgentRegistrationInput> inputs,
    std::span<NavAgentRegistrationResult> results)
{
    NavBatchStats stats;
    stats.input_count = inputs.size();
    const size_t count = std::min(inputs.size(), results.size());
    if (!world.impl || !world.impl->query) {
        stats.rejected_count = inputs.size();
        return stats;
    }
    for (size_t i = 0; i < count; ++i) {
        results[i] = {};
        if (!finite(inputs[i].position)
            || !std::isfinite(inputs[i].clearance)
            || inputs[i].clearance < 0.0f) {
            ++stats.rejected_count;
            continue;
        }
        const auto position = to_detour(inputs[i].position);
        std::array<float, 3> nearest{};
        dtPolyRef polygon = 0;
        if (dtStatusFailed(world.impl->query->findNearestPoly(position.data(), k_nearest_half_extents.data(),
                &world.impl->filter, &polygon, nearest.data()))
            || polygon == 0) {
            results[i].status = NavStatus::off_mesh;
            ++stats.rejected_count;
            continue;
        }
        bool adjusted = false;
        if (!constrain_agent_clearance(
                *world.impl, polygon, nearest, inputs[i].clearance, adjusted)) {
            results[i].status = NavStatus::blocked;
            ++stats.rejected_count;
            continue;
        }
        resolve_agent_height(*world.impl, polygon, nearest);

        uint32_t agent = UINT32_MAX;
        for (uint32_t slot = 0; slot < world.impl->agents.size(); ++slot) {
            if (!world.impl->agents[slot].active) {
                agent = slot;
                break;
            }
        }
        if (agent == UINT32_MAX) {
            if (world.impl->agents.size() >= UINT32_MAX) {
                ++stats.rejected_count;
                continue;
            }
            agent = static_cast<uint32_t>(world.impl->agents.size());
            world.impl->agents.push_back({});
        }
        world.impl->agents[agent] = {
            .polygon = polygon,
            .clearance = inputs[i].clearance,
            .active = 1,
        };
        results[i].agent = agent;
        results[i].position = from_detour(nearest.data());
        results[i].status = adjusted ? NavStatus::clamped : NavStatus::ok;
        ++stats.output_count;
        stats.clamped_count += static_cast<size_t>(adjusted);
    }
    stats.rejected_count += inputs.size() - count;
    return stats;
}

NavBatchStats release_nav_agents(NavWorldState& world, std::span<const uint32_t> agents)
{
    NavBatchStats stats;
    stats.input_count = agents.size();
    if (!world.impl) {
        stats.rejected_count = agents.size();
        return stats;
    }
    for (uint32_t agent : agents) {
        if (agent >= world.impl->agents.size() || !world.impl->agents[agent].active) {
            ++stats.rejected_count;
            continue;
        }
        world.impl->agents[agent] = {};
        ++stats.output_count;
    }
    return stats;
}

NavBatchStats move_nav_agents(
    NavWorldState& world,
    std::span<const NavAgentMotionInput> inputs,
    std::span<NavAgentMotionResult> results)
{
    NavBatchStats stats;
    stats.input_count = inputs.size();
    const size_t count = std::min(inputs.size(), results.size());
    if (!world.impl || !world.impl->query) {
        stats.rejected_count = inputs.size();
        return stats;
    }
    std::array<dtPolyRef, k_max_move_polygons> visited{};
    for (size_t i = 0; i < count; ++i) {
        results[i] = {.position = inputs[i].position};
        if (!finite(inputs[i].position) || !finite(inputs[i].desired_displacement)
            || inputs[i].agent >= world.impl->agents.size()
            || !world.impl->agents[inputs[i].agent].active) {
            ++stats.rejected_count;
            continue;
        }
        const auto start = to_detour(inputs[i].position);
        const auto desired = to_detour(inputs[i].position + inputs[i].desired_displacement);
        std::array<float, 3> resolved{};
        int visited_count = 0;
        auto& agent = world.impl->agents[inputs[i].agent];
        dtPolyRef& polygon = agent.polygon;
        const dtStatus status = world.impl->query->moveAlongSurface(
            polygon, start.data(), desired.data(), &world.impl->filter,
            resolved.data(), visited.data(), &visited_count, static_cast<int>(visited.size()));
        if (dtStatusFailed(status) || visited_count == 0) {
            results[i].status = NavStatus::off_mesh;
            ++stats.rejected_count;
            continue;
        }
        const dtPolyRef original_polygon = polygon;
        polygon = visited[static_cast<size_t>(visited_count - 1)];
        bool clearance_adjusted = false;
        if (!constrain_agent_clearance(*world.impl, polygon, resolved,
                agent.clearance, clearance_adjusted)) {
            polygon = original_polygon;
            results[i].status = NavStatus::blocked;
            ++stats.blocked_count;
            ++stats.output_count;
            continue;
        }
        resolve_agent_height(*world.impl, polygon, resolved);
        results[i].position = from_detour(resolved.data());
        results[i].applied_displacement = results[i].position - inputs[i].position;
        const float requested_sq = glm::dot(inputs[i].desired_displacement, inputs[i].desired_displacement);
        const float applied_sq = glm::dot(results[i].applied_displacement, results[i].applied_displacement);
        if (requested_sq > 0.0f && applied_sq <= 1.0e-12f) {
            results[i].status = NavStatus::blocked;
            ++stats.blocked_count;
        } else if (clearance_adjusted
            || dtStatusDetail(status, DT_BUFFER_TOO_SMALL)
            || glm::distance2(results[i].applied_displacement, inputs[i].desired_displacement) > 1.0e-8f) {
            results[i].status = NavStatus::clamped;
            ++stats.clamped_count;
        } else {
            results[i].status = NavStatus::ok;
        }
        ++stats.output_count;
    }
    stats.rejected_count += inputs.size() - count;
    return stats;
}

} // namespace nw::nav
