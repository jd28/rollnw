#include "NavWorld.hpp"

#include "NavTileBuild.hpp"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <glm/common.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

namespace nw::nav {
namespace {

constexpr int k_max_path_polygons
    = static_cast<int>(maximum_nav_path_polygons);
constexpr int k_max_path_corners
    = static_cast<int>(maximum_nav_path_corners);
constexpr int k_max_move_polygons = 32;
constexpr float k_clearance_epsilon = 1.0e-4f;
constexpr int k_query_nodes = 65535;
constexpr std::array<float, 3> k_nearest_half_extents{10.0f, 50.0f, 10.0f};

size_t detour_polygon_capacity(size_t tile_count) noexcept
{
    if (tile_count == 0
        || tile_count > static_cast<size_t>(std::numeric_limits<unsigned int>::max())) {
        return 0;
    }
#if defined(DT_POLYREF64)
    return static_cast<size_t>(1u) << DT_POLY_BITS;
#else
    constexpr unsigned int minimum_salt_bits = 10;
    constexpr unsigned int reference_bits
        = static_cast<unsigned int>(sizeof(dtPolyRef) * 8u);
    const unsigned int tile_bits = dtIlog2(
        dtNextPow2(static_cast<unsigned int>(tile_count)));
    if (tile_bits + minimum_salt_bits >= reference_bits) return 0;
    const unsigned int polygon_bits
        = reference_bits - tile_bits - minimum_salt_bits;
    return static_cast<size_t>(1u) << polygon_bits;
#endif
}

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

struct DetourNavMeshDeleter {
    void operator()(dtNavMesh* value) const { dtFreeNavMesh(value); }
};

struct DetourQueryDeleter {
    void operator()(dtNavMeshQuery* value) const { dtFreeNavMeshQuery(value); }
};

struct NavPolygonSearchNode {
    float cost = 0.0f;
    float total = 0.0f;
    uint32_t parent = UINT32_MAX;
    uint32_t heap_position = UINT32_MAX;
    uint32_t stamp = 0;
    uint8_t closed = 0;
};

/// Flat graph over the current generated polygons. Detour remains the owner of
/// geometry, projection, raycasts, funnels, and movement; this sidecar removes
/// hash and pointer chasing from the obstructed A* fallback. References are
/// rebuilt after every tile batch and never cross a snapshot boundary.
struct NavPolygonGraph {
    Vector<dtPolyRef> refs;
    Vector<std::array<float, 3>> centers;
    Vector<uint32_t> edge_offsets;
    Vector<uint32_t> edge_nodes;
    Vector<float> edge_costs;
    Vector<uint32_t> mesh_tile_offsets;
    Vector<uint32_t> mesh_polygon_nodes;

    // One world query is already non-reentrant because dtNavMeshQuery owns its
    // scratch. Keep the matching flat A* scratch beside it and allocate only
    // during cold graph construction.
    mutable Vector<NavPolygonSearchNode> search;
    mutable Vector<uint32_t> heap;
    mutable Vector<uint32_t> reverse_path;
    mutable uint32_t search_stamp = 0;
    mutable uint8_t active = 0;
};

} // namespace

struct NavWorldState::Impl {
    struct AgentState {
        dtPolyRef polygon = 0;
        uint8_t active = 0;
    };

    // Payload storage must outlive mesh because Recast worlds add tiles
    // without DT_TILE_FREE_DATA so a failed batch replacement can roll back.
    Vector<NavTileData> tile_payloads;
    std::unique_ptr<dtNavMesh, DetourNavMeshDeleter> mesh;
    std::unique_ptr<dtNavMeshQuery, DetourQueryDeleter> query;
    dtQueryFilter filter;
    const NavAreaBuildSource* area_source = nullptr;
    NavTileBuildConfig tile_config;
    NavTileTriangleRanges surface_ranges;
    NavTileTriangleRanges obstacle_ranges;
    Vector<uint32_t> door_link_offsets;
    Vector<uint32_t> door_link_indices;
    Vector<uint32_t> obstacle_tile_offsets;
    Vector<uint32_t> obstacle_tiles;
    Vector<uint8_t> obstacle_active;
    NavPolygonGraph polygon_graph;
    NavPolygonGraph polygon_graph_spare;
    size_t maximum_tile_polygons = 0;
    uint16_t erosion_cells = 0;
    float cell_size = 0.0f;
    // Movement consumes these fields together through a caller-supplied agent
    // index. Keep each agent on one row instead of touching three independent
    // arrays for every random-indexed input.
    Vector<AgentState> agents;
};

namespace {

uint32_t polygon_graph_node(const dtNavMesh& mesh,
    const NavPolygonGraph& graph, dtPolyRef ref) noexcept
{
    if (ref == 0) return UINT32_MAX;
    const size_t tile = mesh.decodePolyIdTile(ref);
    const size_t polygon = mesh.decodePolyIdPoly(ref);
    if (tile + 1 >= graph.mesh_tile_offsets.size()) return UINT32_MAX;
    const size_t begin = graph.mesh_tile_offsets[tile];
    const size_t end = graph.mesh_tile_offsets[tile + 1];
    if (polygon >= end - begin) return UINT32_MAX;
    const uint32_t node = graph.mesh_polygon_nodes[begin + polygon];
    return node < graph.refs.size() && graph.refs[node] == ref
        ? node
        : UINT32_MAX;
}

size_t polygon_graph_capacity_bytes(const NavPolygonGraph& graph) noexcept
{
    return graph.refs.capacity() * sizeof(graph.refs.front())
        + graph.centers.capacity() * sizeof(graph.centers.front())
        + graph.edge_offsets.capacity() * sizeof(graph.edge_offsets.front())
        + graph.edge_nodes.capacity() * sizeof(graph.edge_nodes.front())
        + graph.edge_costs.capacity() * sizeof(graph.edge_costs.front())
        + graph.mesh_tile_offsets.capacity()
        * sizeof(graph.mesh_tile_offsets.front())
        + graph.mesh_polygon_nodes.capacity()
        * sizeof(graph.mesh_polygon_nodes.front())
        + graph.search.capacity() * sizeof(graph.search.front())
        + graph.heap.capacity() * sizeof(graph.heap.front())
        + graph.reverse_path.capacity()
        * sizeof(graph.reverse_path.front());
}

bool build_polygon_graph(
    const NavWorldState::Impl& impl, NavPolygonGraph& output)
{
    if (!impl.mesh) return false;
    const dtNavMesh& mesh = *impl.mesh;

    NavPolygonGraph& candidate = output;
    candidate.refs.clear();
    candidate.centers.clear();
    candidate.edge_offsets.clear();
    candidate.edge_nodes.clear();
    candidate.edge_costs.clear();
    candidate.mesh_tile_offsets.clear();
    candidate.mesh_polygon_nodes.clear();
    candidate.active = 0;

    candidate.mesh_tile_offsets.assign(
        static_cast<size_t>(mesh.getMaxTiles()) + 1, 0u);
    for (int tile_index = 0; tile_index < mesh.getMaxTiles();
        ++tile_index) {
        const dtMeshTile* tile = mesh.getTile(tile_index);
        if (tile && tile->header && tile->header->polyCount < 0) {
            return false;
        }
        const size_t tile_polygon_count
            = tile && tile->header
            ? static_cast<size_t>(tile->header->polyCount)
            : 0;
        if (candidate.mesh_tile_offsets[tile_index]
            > UINT32_MAX - tile_polygon_count) {
            return false;
        }
        candidate.mesh_tile_offsets[tile_index + 1]
            = candidate.mesh_tile_offsets[tile_index]
            + static_cast<uint32_t>(tile_polygon_count);
    }

    candidate.mesh_polygon_nodes.assign(
        candidate.mesh_tile_offsets.back(), UINT32_MAX);
    candidate.refs.reserve(candidate.mesh_polygon_nodes.size());
    candidate.centers.reserve(candidate.mesh_polygon_nodes.size());
    for (int tile_index = 0; tile_index < mesh.getMaxTiles();
        ++tile_index) {
        const dtMeshTile* tile = mesh.getTile(tile_index);
        if (!tile || !tile->header) continue;
        const dtPolyRef base = mesh.getPolyRefBase(tile);
        for (int polygon_index = 0;
            polygon_index < tile->header->polyCount; ++polygon_index) {
            const auto& polygon = tile->polys[polygon_index];
            const dtPolyRef ref
                = base | static_cast<dtPolyRef>(polygon_index);
            if ((polygon.flags & impl.filter.getIncludeFlags()) == 0
                || (polygon.flags & impl.filter.getExcludeFlags()) != 0) {
                continue;
            }
            if (polygon.vertCount == 0) return false;
            if (candidate.refs.size() == UINT32_MAX) return false;

            std::array<float, 3> center{};
            for (unsigned int vertex = 0; vertex < polygon.vertCount;
                ++vertex) {
                const unsigned short index = polygon.verts[vertex];
                if (index >= tile->header->vertCount) return false;
                center[0] += tile->verts[static_cast<size_t>(index) * 3];
                center[1] += tile->verts[static_cast<size_t>(index) * 3 + 1];
                center[2] += tile->verts[static_cast<size_t>(index) * 3 + 2];
            }
            const float inverse
                = 1.0f / static_cast<float>(polygon.vertCount);
            center[0] *= inverse;
            center[1] *= inverse;
            center[2] *= inverse;
            if (!dtVisfinite(center.data())) return false;

            const uint32_t node
                = static_cast<uint32_t>(candidate.refs.size());
            candidate.refs.push_back(ref);
            candidate.centers.push_back(center);
            candidate.mesh_polygon_nodes[candidate.mesh_tile_offsets[tile_index]
                + static_cast<size_t>(polygon_index)] = node;
        }
    }

    candidate.edge_offsets.reserve(candidate.refs.size() + 1);
    candidate.edge_offsets.push_back(0u);
    for (size_t node = 0; node < candidate.refs.size(); ++node) {
        const size_t tile_index
            = mesh.decodePolyIdTile(candidate.refs[node]);
        const size_t polygon_index
            = mesh.decodePolyIdPoly(candidate.refs[node]);
        const dtMeshTile* tile = tile_index
                < static_cast<size_t>(mesh.getMaxTiles())
            ? mesh.getTile(static_cast<int>(tile_index))
            : nullptr;
        if (!tile || !tile->header
            || polygon_index
                >= static_cast<size_t>(tile->header->polyCount)
            || tile->header->maxLinkCount < 0) {
            return false;
        }
        const dtPoly* polygon = &tile->polys[polygon_index];
        size_t visited_links = 0;
        for (unsigned int link_index = polygon->firstLink;
            link_index != DT_NULL_LINK;
            link_index = tile->links[link_index].next) {
            if (link_index
                    >= static_cast<unsigned int>(tile->header->maxLinkCount)
                || ++visited_links
                    > static_cast<size_t>(tile->header->maxLinkCount)) {
                return false;
            }
            const uint32_t neighbor = polygon_graph_node(
                mesh, candidate, tile->links[link_index].ref);
            if (neighbor == UINT32_MAX) continue;
            const float cost = dtVdist(candidate.centers[node].data(),
                candidate.centers[neighbor].data());
            if (!std::isfinite(cost) || cost < 0.0f) return false;
            if (candidate.edge_nodes.size() == UINT32_MAX) return false;
            candidate.edge_nodes.push_back(neighbor);
            candidate.edge_costs.push_back(cost);
        }
        candidate.edge_offsets.push_back(
            static_cast<uint32_t>(candidate.edge_nodes.size()));
    }

    // Search rows are stamp-initialized on first access. Preserve their size
    // and monotonically increasing stamp across spare-graph rebuilds so a door
    // transition does not zero the full hot scratch arrays.
    candidate.search.resize(candidate.refs.size());
    candidate.heap.resize(candidate.refs.size());
    candidate.reverse_path.resize(candidate.refs.size());
    return true;
}

void reserve_polygon_graph_like(
    NavPolygonGraph& output, const NavPolygonGraph& source)
{
    output.refs.reserve(source.refs.size());
    output.centers.reserve(source.centers.size());
    output.edge_offsets.reserve(source.edge_offsets.size());
    output.edge_nodes.reserve(source.edge_nodes.size());
    output.edge_costs.reserve(source.edge_costs.size());
    output.mesh_tile_offsets.reserve(source.mesh_tile_offsets.size());
    output.mesh_polygon_nodes.reserve(source.mesh_polygon_nodes.size());
    output.search.reserve(source.search.size());
    output.heap.reserve(source.heap.size());
    output.reverse_path.reserve(source.reverse_path.size());
}

class NavPolygonGraphGuard {
public:
    explicit NavPolygonGraphGuard(const NavPolygonGraph& graph) noexcept
        : graph_{graph}
        , acquired_{graph.active == 0}
    {
        if (acquired_) graph_.active = 1;
    }

    ~NavPolygonGraphGuard()
    {
        if (acquired_) graph_.active = 0;
    }

    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    const NavPolygonGraph& graph_;
    bool acquired_ = false;
};

bool polygon_graph_less(
    const NavPolygonGraph& graph, uint32_t lhs, uint32_t rhs) noexcept
{
    const auto& left = graph.search[lhs];
    const auto& right = graph.search[rhs];
    if (left.total != right.total) return left.total < right.total;
    if (left.cost != right.cost) return left.cost > right.cost;
    return lhs < rhs;
}

void polygon_graph_heap_push(
    const NavPolygonGraph& graph, uint32_t node, size_t& heap_size) noexcept
{
    size_t position = heap_size++;
    while (position > 0) {
        const size_t parent = (position - 1) / 2;
        const uint32_t parent_node = graph.heap[parent];
        if (!polygon_graph_less(graph, node, parent_node)) break;
        graph.heap[position] = parent_node;
        graph.search[parent_node].heap_position
            = static_cast<uint32_t>(position);
        position = parent;
    }
    graph.heap[position] = node;
    graph.search[node].heap_position = static_cast<uint32_t>(position);
}

void polygon_graph_heap_decrease(
    const NavPolygonGraph& graph, uint32_t node) noexcept
{
    size_t position = graph.search[node].heap_position;
    while (position > 0) {
        const size_t parent = (position - 1) / 2;
        const uint32_t parent_node = graph.heap[parent];
        if (!polygon_graph_less(graph, node, parent_node)) break;
        graph.heap[position] = parent_node;
        graph.search[parent_node].heap_position
            = static_cast<uint32_t>(position);
        position = parent;
    }
    graph.heap[position] = node;
    graph.search[node].heap_position = static_cast<uint32_t>(position);
}

uint32_t polygon_graph_heap_pop(
    const NavPolygonGraph& graph, size_t& heap_size) noexcept
{
    const uint32_t result = graph.heap[0];
    graph.search[result].heap_position = UINT32_MAX;
    --heap_size;
    if (heap_size == 0) return result;

    const uint32_t moved = graph.heap[heap_size];
    size_t position = 0;
    while (true) {
        const size_t left = position * 2 + 1;
        if (left >= heap_size) break;
        const size_t right = left + 1;
        size_t child = left;
        if (right < heap_size
            && polygon_graph_less(
                graph, graph.heap[right], graph.heap[left])) {
            child = right;
        }
        if (!polygon_graph_less(graph, graph.heap[child], moved)) break;
        graph.heap[position] = graph.heap[child];
        graph.search[graph.heap[position]].heap_position
            = static_cast<uint32_t>(position);
        position = child;
    }
    graph.heap[position] = moved;
    graph.search[moved].heap_position = static_cast<uint32_t>(position);
    return result;
}

NavPolygonSearchNode& begin_polygon_search_node(
    const NavPolygonGraph& graph, uint32_t node, uint32_t stamp) noexcept
{
    auto& row = graph.search[node];
    if (row.stamp != stamp) {
        row = {};
        row.parent = UINT32_MAX;
        row.heap_position = UINT32_MAX;
        row.stamp = stamp;
    }
    return row;
}

dtStatus find_polygon_graph_path(const dtNavMesh& mesh,
    const NavPolygonGraph& graph,
    dtPolyRef start_ref, dtPolyRef end_ref, dtPolyRef* path,
    int* path_count, int maximum_path) noexcept
{
    if (!path_count) return DT_FAILURE | DT_INVALID_PARAM;
    *path_count = 0;
    if (!path || maximum_path <= 0 || graph.refs.empty()
        || graph.refs.size() > UINT32_MAX) {
        return DT_FAILURE | DT_INVALID_PARAM;
    }
    const uint32_t start = polygon_graph_node(mesh, graph, start_ref);
    const uint32_t end = polygon_graph_node(mesh, graph, end_ref);
    if (start == UINT32_MAX || end == UINT32_MAX) {
        return DT_FAILURE | DT_INVALID_PARAM;
    }
    if (start == end) {
        path[0] = start_ref;
        *path_count = 1;
        return DT_SUCCESS;
    }

    NavPolygonGraphGuard guard{graph};
    if (!guard.acquired()) return DT_FAILURE | DT_INVALID_PARAM;
    ++graph.search_stamp;
    if (graph.search_stamp == 0) {
        for (auto& row : graph.search)
            row.stamp = 0;
        graph.search_stamp = 1;
    }
    const uint32_t stamp = graph.search_stamp;
    size_t heap_size = 0;
    auto& start_row = begin_polygon_search_node(graph, start, stamp);
    start_row.cost = 0.0f;
    start_row.total = dtVdist(
        graph.centers[start].data(), graph.centers[end].data());
    polygon_graph_heap_push(graph, start, heap_size);

    uint32_t last_best = start;
    float last_best_heuristic = start_row.total;
    while (heap_size != 0) {
        const uint32_t current
            = polygon_graph_heap_pop(graph, heap_size);
        auto& current_row = graph.search[current];
        current_row.closed = 1;
        if (current == end) {
            last_best = current;
            break;
        }

        const uint32_t edge_begin = graph.edge_offsets[current];
        const uint32_t edge_end = graph.edge_offsets[current + 1];
        for (uint32_t edge = edge_begin; edge < edge_end; ++edge) {
            const uint32_t neighbor = graph.edge_nodes[edge];
            auto& neighbor_row
                = begin_polygon_search_node(graph, neighbor, stamp);
            if (neighbor_row.closed) continue;
            const float cost = current_row.cost + graph.edge_costs[edge];
            if (neighbor_row.heap_position != UINT32_MAX
                && cost >= neighbor_row.cost) {
                continue;
            }
            const float heuristic = neighbor == end
                ? 0.0f
                : dtVdist(graph.centers[neighbor].data(),
                      graph.centers[end].data());
            neighbor_row.parent = current;
            neighbor_row.cost = cost;
            neighbor_row.total = cost + heuristic;
            if (neighbor_row.heap_position == UINT32_MAX) {
                polygon_graph_heap_push(graph, neighbor, heap_size);
            } else {
                polygon_graph_heap_decrease(graph, neighbor);
            }
            if (heuristic < last_best_heuristic) {
                last_best = neighbor;
                last_best_heuristic = heuristic;
            }
        }
    }

    size_t length = 0;
    uint32_t node = last_best;
    while (node != UINT32_MAX && length < graph.reverse_path.size()) {
        graph.reverse_path[length++] = node;
        if (node == start) break;
        const auto& row = graph.search[node];
        if (row.stamp != stamp) return DT_FAILURE;
        node = row.parent;
    }
    if (length == 0 || graph.reverse_path[length - 1] != start) {
        return DT_FAILURE;
    }

    const size_t output_count
        = std::min(length, static_cast<size_t>(maximum_path));
    for (size_t index = 0; index < output_count; ++index) {
        path[index]
            = graph.refs[graph.reverse_path[length - index - 1]];
    }
    *path_count = static_cast<int>(output_count);
    dtStatus status = DT_SUCCESS;
    if (last_best != end) status |= DT_PARTIAL_RESULT;
    if (output_count != length) status |= DT_BUFFER_TOO_SMALL;
    return status;
}

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

void NavRouteArena::clear()
{
    polygons.clear();
    tile_keys.clear();
    traversals.clear();
}

namespace {

bool build_door_link_tile_ranges(std::span<const NavDoorLink> links,
    uint32_t width, uint32_t height, Vector<uint32_t>& offsets,
    Vector<uint32_t>& indices)
{
    const uint64_t tile_count = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0
        || tile_count > std::numeric_limits<uint32_t>::max()
        || links.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    offsets.assign(static_cast<size_t>(tile_count) + 1, 0u);
    for (const auto& link : links) {
        if (!finite(link.start) || !finite(link.end)
            || !std::isfinite(link.radius) || link.radius <= 0.0f
            || link.side > 1
            || link.door_index >= (UINT32_MAX - 1u) / 2u
            || link.start.x < 0.0f || link.start.y < 0.0f) {
            return false;
        }
        const double tile_x = std::floor(
            static_cast<double>(link.start.x)
            / static_cast<double>(nav_tile_size));
        const double tile_y = std::floor(
            static_cast<double>(link.start.y)
            / static_cast<double>(nav_tile_size));
        if (tile_x < 0.0 || tile_y < 0.0
            || tile_x >= static_cast<double>(width)
            || tile_y >= static_cast<double>(height)) {
            return false;
        }
        const size_t tile = static_cast<size_t>(tile_y) * width
            + static_cast<size_t>(tile_x);
        if (offsets[tile + 1] == UINT32_MAX) return false;
        ++offsets[tile + 1];
    }
    for (size_t tile = 0; tile < static_cast<size_t>(tile_count); ++tile) {
        const uint64_t next
            = static_cast<uint64_t>(offsets[tile]) + offsets[tile + 1];
        if (next > UINT32_MAX) return false;
        offsets[tile + 1] = static_cast<uint32_t>(next);
    }
    indices.resize(offsets.back());
    Vector<uint32_t> cursors = offsets;
    for (size_t link_index = 0; link_index < links.size(); ++link_index) {
        const auto& link = links[link_index];
        const uint32_t tile_x = static_cast<uint32_t>(
            std::floor(static_cast<double>(link.start.x)
                / static_cast<double>(nav_tile_size)));
        const uint32_t tile_y = static_cast<uint32_t>(
            std::floor(static_cast<double>(link.start.y)
                / static_cast<double>(nav_tile_size)));
        const size_t tile = static_cast<size_t>(tile_y) * width + tile_x;
        indices[cursors[tile]++] = static_cast<uint32_t>(link_index);
    }
    return true;
}

std::span<const uint32_t> tile_range(const Vector<uint32_t>& offsets,
    const Vector<uint32_t>& values, size_t tile) noexcept
{
    if (tile + 1 >= offsets.size()) return {};
    const uint32_t begin = offsets[tile];
    const uint32_t end = offsets[tile + 1];
    if (begin > end || end > values.size()) return {};
    return std::span<const uint32_t>{values}.subspan(begin, end - begin);
}

bool build_obstacle_tile_ranges(const NavAreaBuildSource& source,
    const NavTileTriangleRanges& obstacle_ranges,
    const Vector<uint32_t>& link_offsets,
    const Vector<uint32_t>& link_indices, Vector<uint32_t>& state_offsets,
    Vector<uint32_t>& state_tiles)
{
    Vector<uint64_t> pairs;
    const size_t tile_count
        = static_cast<size_t>(source.width) * source.height;
    pairs.reserve(obstacle_ranges.triangles.size() + link_indices.size());
    for (size_t tile = 0; tile < tile_count; ++tile) {
        const NavTileCoord coordinate{
            static_cast<uint32_t>(tile % source.width),
            static_cast<uint32_t>(tile / source.width),
        };
        for (uint32_t triangle : obstacle_ranges.tile(
                 coordinate, source.width)) {
            if (triangle >= source.obstacle_owner.size()) return false;
            const uint32_t state = source.obstacle_owner[triangle];
            if (state >= source.obstacle_state_count) return false;
            pairs.push_back(
                static_cast<uint64_t>(state) << 32 | tile);
        }
        for (uint32_t link_index : tile_range(
                 link_offsets, link_indices, tile)) {
            if (link_index >= source.door_links.size()) return false;
            const uint32_t state
                = source.door_links[link_index].active_obstacle_state;
            if (state >= source.obstacle_state_count) return false;
            pairs.push_back(
                static_cast<uint64_t>(state) << 32 | tile);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    if (pairs.size() > UINT32_MAX) return false;
    state_offsets.assign(
        static_cast<size_t>(source.obstacle_state_count) + 1, 0u);
    for (uint64_t pair : pairs) {
        const uint32_t state = static_cast<uint32_t>(pair >> 32);
        if (state >= source.obstacle_state_count
            || state_offsets[static_cast<size_t>(state) + 1] == UINT32_MAX) {
            return false;
        }
        ++state_offsets[static_cast<size_t>(state) + 1];
    }
    for (size_t state = 0; state < source.obstacle_state_count; ++state) {
        const uint64_t next = static_cast<uint64_t>(state_offsets[state])
            + state_offsets[state + 1];
        if (next > UINT32_MAX) return false;
        state_offsets[state + 1] = static_cast<uint32_t>(next);
    }
    state_tiles.resize(pairs.size());
    Vector<uint32_t> cursors = state_offsets;
    for (uint64_t pair : pairs) {
        const uint32_t state = static_cast<uint32_t>(pair >> 32);
        state_tiles[cursors[state]++] = static_cast<uint32_t>(pair);
    }
    return true;
}

} // namespace

NavStatus build_tiled_nav_world(const NavAreaBuildSource& source,
    std::span<const uint8_t> obstacle_active,
    const NavTileBuildConfig& config, NavWorldState& world,
    NavTiledWorldBuildStats& stats)
{
    const auto started = std::chrono::steady_clock::now();
    stats = {};
    world.impl.reset();
    const uint64_t declared_tile_count
        = static_cast<uint64_t>(source.width) * source.height;
    if (source.width == 0 || source.height == 0
        || declared_tile_count > static_cast<uint64_t>(std::numeric_limits<int>::max())
        || obstacle_active.size() != source.obstacle_state_count
        || source.obstacle_surface_ids.size()
            != source.obstacle_indices.size() / 3
        || source.obstacle_owner.size()
            != source.obstacle_indices.size() / 3) {
        return NavStatus::rejected;
    }
    for (uint8_t active : obstacle_active) {
        if (active > 1) return NavStatus::rejected;
    }
    for (uint32_t owner : source.obstacle_owner) {
        if (owner >= source.obstacle_state_count) return NavStatus::rejected;
    }
    for (const auto& link : source.door_links) {
        if (link.active_obstacle_state >= source.obstacle_state_count) {
            return NavStatus::rejected;
        }
    }
    stats.declared_tile_count = static_cast<size_t>(declared_tile_count);

    const float border
        = static_cast<float>(config.erosion_cells + 3u) * config.cell_size;
    NavTileTriangleRanges surface_ranges;
    const auto surface_range_stats = build_nav_tile_triangle_ranges(
        source.surface_vertices, source.surface_indices, source.width,
        source.height, border, surface_ranges);
    if (surface_range_stats.status != NavStatus::ok) {
        return NavStatus::rejected;
    }
    stats.surface_triangle_overlap_count = surface_range_stats.overlap_count;
    NavTileTriangleRanges obstacle_ranges;
    const auto obstacle_range_stats = build_nav_tile_triangle_ranges(
        source.obstacle_vertices, source.obstacle_indices, source.width,
        source.height, border, obstacle_ranges);
    if (obstacle_range_stats.status != NavStatus::ok) {
        return NavStatus::rejected;
    }
    stats.obstacle_triangle_overlap_count = obstacle_range_stats.overlap_count;

    Vector<uint32_t> link_offsets;
    Vector<uint32_t> link_indices;
    if (!build_door_link_tile_ranges(source.door_links, source.width,
            source.height, link_offsets, link_indices)) {
        return NavStatus::rejected;
    }
    Vector<uint32_t> obstacle_tile_offsets;
    Vector<uint32_t> obstacle_tiles;
    if (!build_obstacle_tile_ranges(source, obstacle_ranges, link_offsets,
            link_indices, obstacle_tile_offsets, obstacle_tiles)) {
        return NavStatus::rejected;
    }

    Vector<NavTileData> payloads(stats.declared_tile_count);
    size_t observed_maximum_tile_polygons = 1;
    for (uint32_t y = 0; y < source.height; ++y) {
        for (uint32_t x = 0; x < source.width; ++x) {
            const size_t tile = static_cast<size_t>(y) * source.width + x;
            const NavTileBuildInput input{
                .surface_vertices = source.surface_vertices,
                .surface_indices = source.surface_indices,
                .surface_ids = source.surface_ids,
                .surface_triangles = surface_ranges.tile({x, y}, source.width),
                .surface_walkable = source.surface_walkable,
                .obstacle_vertices = source.obstacle_vertices,
                .obstacle_indices = source.obstacle_indices,
                .obstacle_surface_ids = source.obstacle_surface_ids,
                .obstacle_owner = source.obstacle_owner,
                .obstacle_triangles = obstacle_ranges.tile({x, y}, source.width),
                .obstacle_active = obstacle_active,
                .door_links = source.door_links,
                .door_link_indices = tile_range(
                    link_offsets, link_indices, tile),
                .tile = {x, y},
            };
            const auto built
                = build_nav_tile_data(input, config, payloads[tile]);
            stats.tile_build_nanoseconds += built.build_nanoseconds;
            stats.rasterized_surface_triangle_count
                += built.rasterized_surface_triangle_count;
            stats.rasterized_obstacle_triangle_count
                += built.rasterized_obstacle_triangle_count;
            stats.enabled_door_link_count += built.enabled_door_link_count;
            if (built.status != NavStatus::ok) {
                ++stats.rejected_tile_count;
                return NavStatus::rejected;
            }
            if (payloads[tile].empty()) {
                ++stats.empty_tile_count;
                continue;
            }
            ++stats.built_tile_count;
            stats.polygon_count += built.polygon_count;
            stats.vertex_count += built.vertex_count;
            stats.payload_bytes += built.payload_bytes;
            observed_maximum_tile_polygons = std::max(
                observed_maximum_tile_polygons,
                built.polygon_count + built.enabled_door_link_count);
        }
    }
    const size_t maximum_tile_polygons
        = detour_polygon_capacity(stats.declared_tile_count);
    if (maximum_tile_polygons == 0
        || maximum_tile_polygons
            > static_cast<size_t>(std::numeric_limits<int>::max())
        || observed_maximum_tile_polygons > maximum_tile_polygons) {
        return NavStatus::rejected;
    }
    stats.polygon_capacity_per_tile = maximum_tile_polygons;

    auto impl = std::make_unique<NavWorldState::Impl>();
    impl->mesh.reset(dtAllocNavMesh());
    if (!impl->mesh) return NavStatus::rejected;
    dtNavMeshParams mesh_params{};
    mesh_params.tileWidth = nav_tile_size;
    mesh_params.tileHeight = nav_tile_size;
    mesh_params.maxTiles = static_cast<int>(stats.declared_tile_count);
    mesh_params.maxPolys = static_cast<int>(maximum_tile_polygons);
    if (dtStatusFailed(impl->mesh->init(&mesh_params))) {
        return NavStatus::rejected;
    }
    impl->tile_payloads = std::move(payloads);
    for (auto& payload : impl->tile_payloads) {
        if (payload.empty()) continue;
        if (dtStatusFailed(impl->mesh->addTile(
                payload.data(), payload.size(), 0, 0, nullptr))) {
            return NavStatus::rejected;
        }
    }
    impl->query.reset(dtAllocNavMeshQuery());
    if (!impl->query
        || dtStatusFailed(impl->query->init(impl->mesh.get(), k_query_nodes))) {
        return NavStatus::rejected;
    }
    impl->filter.setIncludeFlags(nav_walkable_flag | nav_door_link_flag);
    const auto graph_started = std::chrono::steady_clock::now();
    if (!build_polygon_graph(*impl, impl->polygon_graph)) {
        return NavStatus::rejected;
    }
    stats.polygon_graph_build_nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - graph_started)
            .count());
    reserve_polygon_graph_like(
        impl->polygon_graph_spare, impl->polygon_graph);
    stats.polygon_graph_bytes
        = polygon_graph_capacity_bytes(impl->polygon_graph)
        + polygon_graph_capacity_bytes(impl->polygon_graph_spare);
    impl->area_source = &source;
    impl->tile_config = config;
    impl->surface_ranges = std::move(surface_ranges);
    impl->obstacle_ranges = std::move(obstacle_ranges);
    impl->door_link_offsets = std::move(link_offsets);
    impl->door_link_indices = std::move(link_indices);
    impl->obstacle_tile_offsets = std::move(obstacle_tile_offsets);
    impl->obstacle_tiles = std::move(obstacle_tiles);
    impl->obstacle_active.assign(
        obstacle_active.begin(), obstacle_active.end());
    impl->maximum_tile_polygons = maximum_tile_polygons;
    impl->erosion_cells = config.erosion_cells;
    impl->cell_size = config.cell_size;
    world.impl = std::move(impl);
    stats.total_nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    stats.status = NavStatus::ok;
    return NavStatus::ok;
}

namespace {

struct NavTileRebuildScratch {
    bool active = false;
    Vector<NavObstacleStateChange> changes;
    Vector<uint8_t> candidate_active;
    Vector<uint32_t> changed_states;
    Vector<uint32_t> tile_keys;
    Vector<NavTileData> payloads;
    Vector<NavTileBuildStats> builds;
    Vector<dtTileRef> old_refs;
};

thread_local NavTileRebuildScratch g_tile_rebuild_scratch;

class NavTileRebuildGuard {
public:
    explicit NavTileRebuildGuard(NavTileRebuildScratch& scratch) noexcept
        : scratch_{scratch}
        , acquired_{!scratch.active}
    {
        if (acquired_) scratch_.active = true;
    }

    ~NavTileRebuildGuard()
    {
        if (acquired_) scratch_.active = false;
    }

    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    NavTileRebuildScratch& scratch_;
    bool acquired_ = false;
};

} // namespace

NavStatus rebuild_nav_tiles(NavWorldState& world,
    const NavAreaBuildSource& source,
    std::span<const NavObstacleStateChange> changes,
    std::span<uint32_t> rebuilt_tile_keys, NavTileRebuildStats& stats)
{
    const auto started = std::chrono::steady_clock::now();
    stats = {};
    stats.input_count = changes.size();
    NavTileRebuildGuard guard{g_tile_rebuild_scratch};
    if (!guard.acquired() || !world.impl
        || world.impl->area_source != &source || !world.impl->mesh
        || world.impl->obstacle_active.size()
            != source.obstacle_state_count) {
        stats.rejected_count = changes.size();
        return NavStatus::rejected;
    }

    auto& impl = *world.impl;
    auto& scratch = g_tile_rebuild_scratch;
    scratch.changes.assign(changes.begin(), changes.end());
    std::sort(scratch.changes.begin(), scratch.changes.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.obstacle_state != rhs.obstacle_state) {
                return lhs.obstacle_state < rhs.obstacle_state;
            }
            return lhs.active < rhs.active;
        });
    scratch.candidate_active = impl.obstacle_active;
    scratch.changed_states.clear();
    for (size_t begin = 0; begin < scratch.changes.size();) {
        size_t end = begin + 1;
        while (end < scratch.changes.size()
            && scratch.changes[end].obstacle_state
                == scratch.changes[begin].obstacle_state) {
            ++end;
        }
        const auto& change = scratch.changes[begin];
        if (change.obstacle_state >= source.obstacle_state_count
            || change.active > 1
            || scratch.changes[end - 1].active != change.active) {
            stats.rejected_count = end - begin;
            return NavStatus::rejected;
        }
        stats.duplicate_count += end - begin - 1;
        if (scratch.candidate_active[change.obstacle_state]
            != change.active) {
            scratch.candidate_active[change.obstacle_state] = change.active;
            scratch.changed_states.push_back(change.obstacle_state);
        }
        begin = end;
    }
    stats.changed_state_count = scratch.changed_states.size();

    scratch.tile_keys.clear();
    for (uint32_t state : scratch.changed_states) {
        if (static_cast<size_t>(state) + 1
            >= impl.obstacle_tile_offsets.size()) {
            stats.rejected_count = 1;
            return NavStatus::rejected;
        }
        const uint32_t begin = impl.obstacle_tile_offsets[state];
        const uint32_t end = impl.obstacle_tile_offsets[state + 1];
        if (begin > end || end > impl.obstacle_tiles.size()) {
            stats.rejected_count = 1;
            return NavStatus::rejected;
        }
        scratch.tile_keys.insert(scratch.tile_keys.end(),
            impl.obstacle_tiles.begin() + begin,
            impl.obstacle_tiles.begin() + end);
    }
    std::sort(scratch.tile_keys.begin(), scratch.tile_keys.end());
    scratch.tile_keys.erase(
        std::unique(scratch.tile_keys.begin(), scratch.tile_keys.end()),
        scratch.tile_keys.end());
    stats.affected_tile_count = scratch.tile_keys.size();
    if (rebuilt_tile_keys.size() < scratch.tile_keys.size()) {
        stats.status = NavStatus::output_full;
        return stats.status;
    }
    if (scratch.tile_keys.empty()) {
        impl.obstacle_active = scratch.candidate_active;
        stats.status = NavStatus::ok;
        stats.total_nanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        return stats.status;
    }

    scratch.payloads.clear();
    scratch.payloads.resize(scratch.tile_keys.size());
    scratch.builds.clear();
    scratch.builds.resize(scratch.tile_keys.size());
    scratch.old_refs.assign(scratch.tile_keys.size(), 0);
    for (size_t row = 0; row < scratch.tile_keys.size(); ++row) {
        const uint32_t tile_key = scratch.tile_keys[row];
        if (tile_key
            >= static_cast<uint64_t>(source.width) * source.height) {
            ++stats.rejected_count;
            return NavStatus::rejected;
        }
    }

    std::atomic_size_t next_tile{0};
    const auto build_tiles = [&] {
        for (;;) {
            const size_t row
                = next_tile.fetch_add(1, std::memory_order_relaxed);
            if (row >= scratch.tile_keys.size()) return;
            const uint32_t tile_key = scratch.tile_keys[row];
            const NavTileCoord coordinate{
                tile_key % source.width,
                tile_key / source.width,
            };
            const NavTileBuildInput input{
                .surface_vertices = source.surface_vertices,
                .surface_indices = source.surface_indices,
                .surface_ids = source.surface_ids,
                .surface_triangles = impl.surface_ranges.tile(
                    coordinate, source.width),
                .surface_walkable = source.surface_walkable,
                .obstacle_vertices = source.obstacle_vertices,
                .obstacle_indices = source.obstacle_indices,
                .obstacle_surface_ids = source.obstacle_surface_ids,
                .obstacle_owner = source.obstacle_owner,
                .obstacle_triangles = impl.obstacle_ranges.tile(
                    coordinate, source.width),
                .obstacle_active = scratch.candidate_active,
                .door_links = source.door_links,
                .door_link_indices = tile_range(impl.door_link_offsets,
                    impl.door_link_indices, tile_key),
                .tile = coordinate,
            };
            scratch.builds[row] = build_nav_tile_data(
                input, impl.tile_config, scratch.payloads[row]);
        }
    };

    const size_t available_threads
        = std::max<size_t>(1, std::thread::hardware_concurrency());
    const size_t worker_count = std::min(
        {scratch.tile_keys.size(), available_threads, size_t{4}});
    // Tile payloads do not share mutable Recast or Detour state. Build up to
    // four rows concurrently, then mutate the live Detour mesh serially so a
    // failed row still leaves the installed snapshot untouched.
    std::array<std::jthread, 3> workers;
    for (size_t worker = 1; worker < worker_count; ++worker) {
        workers[worker - 1] = std::jthread{build_tiles};
    }
    build_tiles();
    for (size_t worker = 1; worker < worker_count; ++worker) {
        workers[worker - 1].join();
    }

    for (size_t row = 0; row < scratch.tile_keys.size(); ++row) {
        const auto& built = scratch.builds[row];
        stats.tile_build_nanoseconds += built.build_nanoseconds;
        stats.payload_bytes += built.payload_bytes;
        if (built.status != NavStatus::ok
            || built.polygon_count + built.enabled_door_link_count
                > impl.maximum_tile_polygons) {
            ++stats.rejected_count;
            return NavStatus::rejected;
        }
        stats.empty_tile_count += scratch.payloads[row].empty();
    }

    const auto restore_old_tiles = [&](size_t count) {
        bool restored = true;
        for (size_t row = 0; row < count; ++row) {
            if (scratch.old_refs[row] == 0) continue;
            const uint32_t tile_key = scratch.tile_keys[row];
            auto& payload = impl.tile_payloads[tile_key];
            restored = dtStatusSucceed(impl.mesh->addTile(payload.data(),
                           payload.size(), 0, scratch.old_refs[row], nullptr))
                && restored;
        }
        return restored;
    };

    size_t removed_count = 0;
    for (; removed_count < scratch.tile_keys.size(); ++removed_count) {
        const uint32_t tile_key = scratch.tile_keys[removed_count];
        const int x = static_cast<int>(tile_key % source.width);
        const int y = static_cast<int>(tile_key / source.width);
        const dtTileRef old_ref = impl.mesh->getTileRefAt(x, y, 0);
        scratch.old_refs[removed_count] = old_ref;
        if (old_ref == 0) continue;
        unsigned char* old_data = nullptr;
        int old_size = 0;
        if (dtStatusFailed(
                impl.mesh->removeTile(old_ref, &old_data, &old_size))) {
            (void)restore_old_tiles(removed_count);
            ++stats.rejected_count;
            return NavStatus::rejected;
        }
    }

    const auto rollback = [&]() {
        bool restored = true;
        for (uint32_t tile_key : scratch.tile_keys) {
            const int x = static_cast<int>(tile_key % source.width);
            const int y = static_cast<int>(tile_key / source.width);
            const dtTileRef current_ref = impl.mesh->getTileRefAt(x, y, 0);
            if (current_ref == 0) continue;
            unsigned char* data = nullptr;
            int size = 0;
            restored = dtStatusSucceed(
                           impl.mesh->removeTile(current_ref, &data, &size))
                && restored;
        }
        return restore_old_tiles(scratch.tile_keys.size()) && restored;
    };

    const auto add_new_tile = [&](size_t row, dtTileRef last_ref) {
        auto& payload = scratch.payloads[row];
        return payload.empty()
            || dtStatusSucceed(impl.mesh->addTile(
                payload.data(), payload.size(), 0, last_ref, nullptr));
    };
    for (size_t row = 0; row < scratch.tile_keys.size(); ++row) {
        if (scratch.old_refs[row] != 0
            && !add_new_tile(row, scratch.old_refs[row])) {
            (void)rollback();
            ++stats.rejected_count;
            return NavStatus::rejected;
        }
    }
    for (size_t row = 0; row < scratch.tile_keys.size(); ++row) {
        if (scratch.old_refs[row] == 0 && !add_new_tile(row, 0)) {
            (void)rollback();
            ++stats.rejected_count;
            return NavStatus::rejected;
        }
    }

    const auto graph_started = std::chrono::steady_clock::now();
    if (!build_polygon_graph(impl, impl.polygon_graph_spare)) {
        (void)rollback();
        ++stats.rejected_count;
        return NavStatus::rejected;
    }
    stats.polygon_graph_build_nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - graph_started)
            .count());

    impl.obstacle_active = scratch.candidate_active;
    for (size_t row = 0; row < scratch.tile_keys.size(); ++row) {
        const uint32_t tile_key = scratch.tile_keys[row];
        impl.tile_payloads[tile_key] = std::move(scratch.payloads[row]);
        rebuilt_tile_keys[row] = tile_key;
    }
    std::swap(impl.polygon_graph, impl.polygon_graph_spare);
    for (auto& agent : impl.agents) {
        if (agent.active) agent.polygon = 0;
    }
    stats.rebuilt_tile_count = scratch.tile_keys.size();
    stats.total_nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    stats.status = NavStatus::ok;
    return stats.status;
}

NavBatchStats invalidate_nav_routes(
    const NavRouteArena& arena,
    std::span<const NavRouteInvalidationInput> inputs,
    std::span<const uint32_t> rebuilt_tile_keys,
    std::span<NavRouteInvalidationResult> results)
{
    NavBatchStats stats{.input_count = inputs.size()};
    const size_t count = std::min(inputs.size(), results.size());
    const bool valid_changes = std::adjacent_find(
                                   rebuilt_tile_keys.begin(),
                                   rebuilt_tile_keys.end(),
                                   std::greater_equal<uint32_t>{})
        == rebuilt_tile_keys.end();
    for (size_t index = 0; index < count; ++index) {
        auto& output = results[index];
        output = {};
        const auto& input = inputs[index];
        const auto& route = input.route;
        const size_t polygon_begin = route.polygon_offset;
        const size_t polygon_end = polygon_begin + route.polygon_count;
        const size_t tile_begin = route.tile_offset;
        const size_t tile_end = tile_begin + route.tile_count;
        if (!valid_changes
            || polygon_end < polygon_begin
            || polygon_end > arena.polygons.size()
            || tile_end < tile_begin || tile_end > arena.tile_keys.size()
            || input.first_remaining_polygon > route.polygon_count) {
            ++stats.rejected_count;
            continue;
        }

        output.status = NavStatus::ok;
        ++stats.output_count;
        if (rebuilt_tile_keys.empty()
            || !std::ranges::any_of(
                std::span{arena.tile_keys}.subspan(
                    tile_begin, route.tile_count),
                [&](uint32_t key) {
                    return std::binary_search(rebuilt_tile_keys.begin(),
                        rebuilt_tile_keys.end(), key);
                })) {
            continue;
        }

        const size_t remaining_begin
            = polygon_begin + input.first_remaining_polygon;
        output.invalidated = std::ranges::any_of(
            std::span{arena.polygons}.subspan(
                remaining_begin, polygon_end - remaining_begin),
            [&](const NavRoutePolygon& polygon) {
                return std::binary_search(rebuilt_tile_keys.begin(),
                    rebuilt_tile_keys.end(), polygon.tile_key);
            });
    }
    stats.rejected_count += inputs.size() - count;
    return stats;
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
    if (impl.area_source) {
        const auto& source = *impl.area_source;
        const size_t triangle_count = source.obstacle_indices.size() / 3;
        for (size_t triangle = 0; triangle < triangle_count; ++triangle) {
            ++stats.input_count;
            if (triangle >= source.obstacle_owner.size()) {
                ++stats.rejected_count;
                continue;
            }
            const uint32_t owner = source.obstacle_owner[triangle];
            if (owner >= impl.obstacle_active.size()
                || impl.obstacle_active[owner] == 0) {
                continue;
            }
            const size_t index = triangle * 3;
            const uint32_t a = source.obstacle_indices[index];
            const uint32_t b = source.obstacle_indices[index + 1];
            const uint32_t c = source.obstacle_indices[index + 2];
            if (a >= source.obstacle_vertices.size()
                || b >= source.obstacle_vertices.size()
                || c >= source.obstacle_vertices.size()) {
                ++stats.rejected_count;
                continue;
            }
            const auto& va = source.obstacle_vertices[a];
            const auto& vb = source.obstacle_vertices[b];
            const auto& vc = source.obstacle_vertices[c];
            if (!finite(va) || !finite(vb) || !finite(vc)) {
                ++stats.rejected_count;
                continue;
            }
            triangles.push_back(
                {va, vb, vc, NavDebugPolygonState::blocked});
            ++stats.output_count;
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

static NavBatchStats find_nav_paths_impl(
    const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena,
    NavRouteArena* route_arena,
    std::span<NavPathResult> results)
{
    NavBatchStats stats;
    stats.input_count = requests.size();
    corner_arena.clear();
    if (route_arena) route_arena->clear();
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
    std::array<unsigned char, k_max_path_corners> corner_flags;
    std::array<dtPolyRef, k_max_path_polygons> cached_graph_path;
    dtPolyRef cached_graph_start = 0;
    dtPolyRef cached_graph_end = 0;
    int cached_graph_path_count = 0;
    dtStatus cached_graph_status = DT_FAILURE;
    for (size_t i = 0; i < count; ++i) {
        results[i] = {};
        if (!finite(requests[i].start) || !finite(requests[i].end)) {
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
            float direct_hit = 0.0f;
            std::array<float, 3> direct_normal{};
            const dtStatus direct_status = world.impl->query->raycast(
                start_ref, nearest_start.data(), nearest_end.data(),
                &world.impl->filter, &direct_hit, direct_normal.data(),
                path.data(), &path_count, static_cast<int>(path.size()));
            const bool direct = dtStatusSucceed(direct_status)
                && path_count > 0 && direct_hit > 1.0f
                && path[static_cast<size_t>(path_count - 1)] == end_ref;
            if (direct) {
                path_status = direct_status;
            } else {
                path_count = 0;
                if (cached_graph_start == start_ref
                    && cached_graph_end == end_ref
                    && cached_graph_path_count > 0) {
                    path_count = cached_graph_path_count;
                    path_status = cached_graph_status;
                    std::copy_n(cached_graph_path.begin(), path_count,
                        path.begin());
                } else {
                    path_status = find_polygon_graph_path(*world.impl->mesh,
                        world.impl->polygon_graph, start_ref, end_ref,
                        path.data(), &path_count,
                        static_cast<int>(path.size()));
                    if (dtStatusSucceed(path_status) && path_count > 0
                        && path[static_cast<size_t>(path_count - 1)]
                            == end_ref
                        && !dtStatusDetail(
                            path_status, DT_PARTIAL_RESULT)) {
                        cached_graph_start = start_ref;
                        cached_graph_end = end_ref;
                        cached_graph_path_count = path_count;
                        cached_graph_status = path_status;
                        std::copy_n(path.begin(), path_count,
                            cached_graph_path.begin());
                    }
                }
            }
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
            corners.data(), corner_flags.data(), corner_polygons.data(),
            &corner_count, k_max_path_corners);
        if (dtStatusFailed(straight_status)) {
            ++stats.rejected_count;
            continue;
        }

        bool valid_path = true;
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

            std::array<float, 3> adjusted_corner{
                corners[corner_index * 3],
                corners[corner_index * 3 + 1],
                corners[corner_index * 3 + 2],
            };
            resolve_agent_height(*world.impl, polygon, adjusted_corner);
            corners[corner_index * 3] = adjusted_corner[0];
            corners[corner_index * 3 + 1] = adjusted_corner[1];
            corners[corner_index * 3 + 2] = adjusted_corner[2];
        }
        if (!valid_path) {
            results[i].status = NavStatus::blocked;
            ++stats.blocked_count;
            continue;
        }
        if (corner_arena.size() > UINT32_MAX - static_cast<size_t>(corner_count)
            || (route_arena
                && corner_arena.capacity() - corner_arena.size()
                    < static_cast<size_t>(corner_count))) {
            results[i].status = NavStatus::output_full;
            ++stats.rejected_count;
            continue;
        }
        std::array<NavRoutePolygon, k_max_path_polygons> route_polygons{};
        std::array<uint32_t, k_max_path_polygons> route_tiles{};
        size_t route_tile_count = 0;
        std::array<NavDoorTraversal, k_max_path_corners> traversals{};
        size_t traversal_count = 0;
        if (route_arena) {
            bool valid_route = true;
            for (int polygon_index = 0; polygon_index < path_count;
                ++polygon_index) {
                const dtMeshTile* tile = nullptr;
                const dtPoly* polygon = nullptr;
                if (dtStatusFailed(world.impl->mesh->getTileAndPolyByRef(
                        path[static_cast<size_t>(polygon_index)], &tile,
                        &polygon))
                    || !tile || !tile->header || !polygon
                    || tile->header->x < 0 || tile->header->y < 0
                    || static_cast<uint32_t>(tile->header->x)
                        >= world.impl->area_source->width
                    || static_cast<uint32_t>(tile->header->y)
                        >= world.impl->area_source->height) {
                    valid_route = false;
                    break;
                }
                const uint32_t tile_key
                    = static_cast<uint32_t>(tile->header->y)
                        * world.impl->area_source->width
                    + static_cast<uint32_t>(tile->header->x);
                route_polygons[static_cast<size_t>(polygon_index)] = {
                    .polygon_ref = static_cast<uint64_t>(
                        path[static_cast<size_t>(polygon_index)]),
                    .tile_key = tile_key,
                };
                route_tiles[static_cast<size_t>(polygon_index)] = tile_key;
            }
            std::sort(route_tiles.begin(), route_tiles.begin() + path_count);
            route_tile_count = static_cast<size_t>(std::unique(
                                                       route_tiles.begin(), route_tiles.begin() + path_count)
                - route_tiles.begin());
            for (int corner = 0; valid_route && corner < corner_count;
                ++corner) {
                if ((corner_flags[static_cast<size_t>(corner)]
                        & DT_STRAIGHTPATH_OFFMESH_CONNECTION)
                    == 0) {
                    continue;
                }
                const auto* connection
                    = world.impl->mesh->getOffMeshConnectionByRef(
                        corner_polygons[static_cast<size_t>(corner)]);
                if (!connection || connection->userId == 0) {
                    valid_route = false;
                    break;
                }
                const uint32_t tag = connection->userId - 1u;
                traversals[traversal_count++] = {
                    .door_index = tag / 2u,
                    .corner = static_cast<uint32_t>(corner),
                    .side = static_cast<uint8_t>(tag % 2u),
                };
            }
            if (!valid_route
                || route_arena->polygons.size()
                    > UINT32_MAX - static_cast<size_t>(path_count)
                || route_arena->tile_keys.size()
                    > UINT32_MAX - route_tile_count
                || route_arena->traversals.size()
                    > UINT32_MAX - traversal_count) {
                results[i].status = valid_route ? NavStatus::output_full
                                                : NavStatus::rejected;
                ++stats.rejected_count;
                continue;
            }
            if (route_arena->polygons.capacity()
                        - route_arena->polygons.size()
                    < static_cast<size_t>(path_count)
                || route_arena->tile_keys.capacity()
                        - route_arena->tile_keys.size()
                    < route_tile_count
                || route_arena->traversals.capacity()
                        - route_arena->traversals.size()
                    < traversal_count) {
                results[i].status = NavStatus::output_full;
                ++stats.rejected_count;
                continue;
            }
        }
        results[i].corner_offset = static_cast<uint32_t>(corner_arena.size());
        results[i].corner_count = static_cast<uint32_t>(corner_count);
        if (route_arena) {
            results[i].polygon_offset
                = static_cast<uint32_t>(route_arena->polygons.size());
            results[i].polygon_count = static_cast<uint32_t>(path_count);
            results[i].tile_offset
                = static_cast<uint32_t>(route_arena->tile_keys.size());
            results[i].tile_count = static_cast<uint32_t>(route_tile_count);
            results[i].traversal_offset
                = static_cast<uint32_t>(route_arena->traversals.size());
            results[i].traversal_count
                = static_cast<uint32_t>(traversal_count);
        }
        results[i].status = partial_path
                || dtStatusDetail(path_status, DT_BUFFER_TOO_SMALL)
                || dtStatusDetail(straight_status, DT_BUFFER_TOO_SMALL)
            ? NavStatus::clamped
            : NavStatus::ok;
        for (int corner = 0; corner < corner_count; ++corner) {
            corner_arena.push_back(from_detour(&corners[static_cast<size_t>(corner) * 3]));
        }
        if (route_arena) {
            route_arena->polygons.insert(route_arena->polygons.end(),
                route_polygons.begin(), route_polygons.begin() + path_count);
            route_arena->tile_keys.insert(route_arena->tile_keys.end(),
                route_tiles.begin(), route_tiles.begin() + route_tile_count);
            route_arena->traversals.insert(route_arena->traversals.end(),
                traversals.begin(), traversals.begin() + traversal_count);
        }
        ++stats.output_count;
        stats.clamped_count += static_cast<size_t>(results[i].status == NavStatus::clamped);
    }
    stats.rejected_count += requests.size() - count;
    return stats;
}

NavBatchStats find_nav_paths(const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena,
    std::span<NavPathResult> results)
{
    return find_nav_paths_impl(
        world, requests, corner_arena, nullptr, results);
}

NavBatchStats find_nav_paths(const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena, NavRouteArena& route_arena,
    std::span<NavPathResult> results)
{
    return find_nav_paths_impl(
        world, requests, corner_arena, &route_arena, results);
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
        if (!finite(inputs[i].position)) {
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
            .active = 1,
        };
        results[i].agent = agent;
        results[i].position = from_detour(nearest.data());
        const float delta_x = nearest[0] - position[0];
        const float delta_z = nearest[2] - position[2];
        const bool adjusted = delta_x * delta_x + delta_z * delta_z
            > k_clearance_epsilon * k_clearance_epsilon;
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
        if (polygon == 0
            || !world.impl->mesh->isValidPolyRef(polygon)) {
            std::array<float, 3> nearest{};
            if (dtStatusFailed(world.impl->query->findNearestPoly(start.data(),
                    k_nearest_half_extents.data(), &world.impl->filter,
                    &polygon, nearest.data()))
                || polygon == 0) {
                results[i].status = NavStatus::off_mesh;
                ++stats.rejected_count;
                continue;
            }
        }
        const dtStatus status = world.impl->query->moveAlongSurface(
            polygon, start.data(), desired.data(), &world.impl->filter,
            resolved.data(), visited.data(), &visited_count, static_cast<int>(visited.size()));
        if (dtStatusFailed(status) || visited_count == 0) {
            results[i].status = NavStatus::off_mesh;
            ++stats.rejected_count;
            continue;
        }
        polygon = visited[static_cast<size_t>(visited_count - 1)];
        resolve_agent_height(*world.impl, polygon, resolved);
        results[i].position = from_detour(resolved.data());
        results[i].applied_displacement = results[i].position - inputs[i].position;
        const float requested_sq = glm::dot(inputs[i].desired_displacement, inputs[i].desired_displacement);
        const float applied_sq = glm::dot(results[i].applied_displacement, results[i].applied_displacement);
        if (requested_sq > 0.0f && applied_sq <= 1.0e-12f) {
            results[i].status = NavStatus::blocked;
            ++stats.blocked_count;
        } else if (dtStatusDetail(status, DT_BUFFER_TOO_SMALL)
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
