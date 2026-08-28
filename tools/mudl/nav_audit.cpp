#include "app_runtime.hpp"
#include "mudl_commands.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavTileBuild.hpp>
#include <nw/nav/NavWorld.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>

namespace mudl {
namespace {

struct AuditConfigStats {
    uint64_t tile_count = 0;
    uint64_t built_tile_count = 0;
    uint64_t empty_tile_count = 0;
    uint64_t rejected_tile_count = 0;
    uint64_t polygon_count = 0;
    uint64_t payload_bytes = 0;
    uint64_t cross_tile_links = 0;
    uint64_t build_nanoseconds = 0;
    uint64_t projection_count = 0;
    uint64_t projection_failure_count = 0;
    uint64_t surface_error_failure_count = 0;
    uint64_t route_count = 0;
    uint64_t lost_route_count = 0;
    uint64_t polygon_graph_route_count = 0;
    uint64_t polygon_graph_clamped_route_count = 0;
    uint64_t polygon_graph_lost_route_count = 0;
    uint64_t disconnected_component_pair_count = 0;
    uint64_t unexpected_route_count = 0;
    uint64_t polygon_graph_unexpected_route_count = 0;
    uint64_t polygon_graph_bytes = 0;
    uint64_t polygon_graph_build_nanoseconds = 0;
    float maximum_horizontal_projection = 0.0f;
    float maximum_surface_error = 0.0f;
    glm::vec3 maximum_surface_error_input{0.0f};
    glm::vec3 maximum_surface_error_output{0.0f};
    uint64_t authored_layer_count_at_maximum_error = 0;
    float authored_layer_minimum_at_maximum_error = 0.0f;
    float authored_layer_maximum_at_maximum_error = 0.0f;
    float authored_triangle_vertex_minimum_at_maximum_error = 0.0f;
    float authored_triangle_vertex_maximum_at_maximum_error = 0.0f;
    bool passed = false;
};

struct AuditSelection {
    nw::nav::NavTileBuildConfig config;
    AuditConfigStats stats;
    bool found = false;
};

struct SyntheticGridStats {
    uint64_t tile_count = 0;
    uint64_t built_tile_count = 0;
    uint64_t empty_tile_count = 0;
    uint64_t rejected_tile_count = 0;
    uint64_t polygon_count = 0;
    uint64_t payload_bytes = 0;
    uint64_t polygon_graph_bytes = 0;
    uint64_t tile_build_nanoseconds = 0;
    uint64_t polygon_graph_build_nanoseconds = 0;
    uint64_t total_nanoseconds = 0;
};

struct AuthoredSample {
    glm::vec3 position{0.0f};
    uint32_t component = UINT32_MAX;
};

struct ProjectedSample {
    std::array<float, 3> position{};
    dtPolyRef polygon = 0;
    uint32_t component = UINT32_MAX;
};

struct AuthoredSampleStats {
    uint64_t walkable_triangle_count = 0;
    uint64_t adjacency_component_count = 0;
    uint64_t component_count = 0;
    uint64_t sampled_component_count = 0;
    uint64_t rejected_sample_count = 0;
};

constexpr size_t k_max_path_polygons = 4096;
constexpr uint64_t k_max_component_pair_audits = 65536;

SyntheticGridStats build_synthetic_grid(
    uint32_t width, uint32_t height,
    const nw::nav::NavTileBuildConfig& config)
{
    SyntheticGridStats stats;
    const float world_width
        = static_cast<float>(width) * nw::nav::nav_tile_size;
    const float world_height
        = static_cast<float>(height) * nw::nav::nav_tile_size;
    nw::nav::NavAreaBuildSource source;
    source.surface_vertices = {
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{world_width, 0.0f, 0.0f},
        glm::vec3{0.0f, world_height, 0.0f},
        glm::vec3{world_width, world_height, 0.0f},
    };
    source.surface_indices = {0, 2, 1, 1, 2, 3};
    source.surface_ids = {1, 1};
    source.surface_walkable = {0, 1};
    source.width = width;
    source.height = height;
    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    (void)nw::nav::build_tiled_nav_world(
        source, {}, config, world, build);
    stats.tile_count = build.declared_tile_count;
    stats.built_tile_count = build.built_tile_count;
    stats.empty_tile_count = build.empty_tile_count;
    stats.rejected_tile_count = build.rejected_tile_count;
    stats.polygon_count = build.polygon_count;
    stats.payload_bytes = build.payload_bytes;
    stats.polygon_graph_bytes = build.polygon_graph_bytes;
    stats.tile_build_nanoseconds = build.tile_build_nanoseconds;
    stats.polygon_graph_build_nanoseconds
        = build.polygon_graph_build_nanoseconds;
    stats.total_nanoseconds = build.total_nanoseconds;
    return stats;
}

std::array<float, 3> to_detour(const glm::vec3& value)
{
    return {value.x, value.z, value.y};
}

bool walkable_triangle(const nw::nav::NavGeometry& geometry,
    std::span<const uint8_t> surface_walkable, size_t triangle)
{
    return triangle < geometry.surface.size()
        && geometry.surface[triangle] < surface_walkable.size()
        && surface_walkable[geometry.surface[triangle]] != 0;
}

struct AuthoredSeamEdge {
    uint64_t seam = 0;
    int64_t along_minimum = 0;
    int64_t along_maximum = 0;
    float height_at_minimum = 0.0f;
    float height_at_maximum = 0.0f;
    uint32_t component = UINT32_MAX;
    uint32_t owner = UINT32_MAX;
};

bool quantize_authored_axis(float value, int64_t& output)
{
    constexpr double inverse_step = 1000.0;
    const double scaled = std::round(static_cast<double>(value) * inverse_step);
    if (!std::isfinite(scaled)
        || scaled < static_cast<double>(std::numeric_limits<int64_t>::min())
        || scaled > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    output = static_cast<int64_t>(scaled);
    return true;
}

bool make_authored_seam_edge(const nw::nav::NavGeometry& geometry,
    std::span<const uint32_t> components, size_t source, uint32_t width,
    uint32_t height, AuthoredSeamEdge& output)
{
    constexpr int64_t tile_span = 10000;
    const size_t triangle = source / 3;
    const size_t triangle_offset = triangle * 3;
    const size_t local_edge = source % 3;
    if (triangle >= components.size()
        || components[triangle] == UINT32_MAX
        || triangle >= geometry.owner.size()) {
        return false;
    }
    const uint32_t first_index = geometry.indices[source];
    const uint32_t second_index
        = geometry.indices[triangle_offset + (local_edge + 1) % 3];
    if (first_index >= geometry.vertices.size()
        || second_index >= geometry.vertices.size()) {
        return false;
    }
    const auto& first = geometry.vertices[first_index];
    const auto& second = geometry.vertices[second_index];
    if (!std::isfinite(first.x) || !std::isfinite(first.y)
        || !std::isfinite(first.z) || !std::isfinite(second.x)
        || !std::isfinite(second.y) || !std::isfinite(second.z)) {
        return false;
    }

    const uint32_t owner = geometry.owner[triangle];
    const uint64_t tile_count = static_cast<uint64_t>(width) * height;
    if (owner >= tile_count) return false;
    const uint32_t tile_x = owner % width;
    const uint32_t tile_y = owner / width;
    int64_t first_x = 0;
    int64_t first_y = 0;
    int64_t second_x = 0;
    int64_t second_y = 0;
    if (!quantize_authored_axis(first.x, first_x)
        || !quantize_authored_axis(first.y, first_y)
        || !quantize_authored_axis(second.x, second_x)
        || !quantize_authored_axis(second.y, second_y)) {
        return false;
    }

    const int64_t minimum_x = static_cast<int64_t>(tile_x) * tile_span;
    const int64_t minimum_y = static_cast<int64_t>(tile_y) * tile_span;
    uint32_t neighbor = UINT32_MAX;
    int64_t first_along = 0;
    int64_t second_along = 0;
    if (first_x == minimum_x && second_x == minimum_x && tile_x > 0) {
        neighbor = owner - 1;
        first_along = first_y;
        second_along = second_y;
    } else if (first_y == minimum_y + tile_span
        && second_y == minimum_y + tile_span && tile_y + 1 < height) {
        neighbor = owner + width;
        first_along = first_x;
        second_along = second_x;
    } else if (first_x == minimum_x + tile_span
        && second_x == minimum_x + tile_span && tile_x + 1 < width) {
        neighbor = owner + 1;
        first_along = first_y;
        second_along = second_y;
    } else if (first_y == minimum_y && second_y == minimum_y
        && tile_y > 0) {
        neighbor = owner - width;
        first_along = first_x;
        second_along = second_x;
    } else {
        return false;
    }
    if (first_along == second_along) return false;

    output.seam = (static_cast<uint64_t>(std::min(owner, neighbor)) << 32)
        | std::max(owner, neighbor);
    output.along_minimum = std::min(first_along, second_along);
    output.along_maximum = std::max(first_along, second_along);
    output.component = components[triangle];
    output.owner = owner;
    if (first_along < second_along) {
        output.height_at_minimum = first.z;
        output.height_at_maximum = second.z;
    } else {
        output.height_at_minimum = second.z;
        output.height_at_maximum = first.z;
    }
    return true;
}

double authored_seam_height_at(
    const AuthoredSeamEdge& edge, int64_t along)
{
    const double extent
        = static_cast<double>(edge.along_maximum - edge.along_minimum);
    const double fraction
        = static_cast<double>(along - edge.along_minimum) / extent;
    return static_cast<double>(edge.height_at_minimum)
        + static_cast<double>(edge.height_at_maximum - edge.height_at_minimum)
        * fraction;
}

bool authored_seam_edges_connect(const AuthoredSeamEdge& first,
    const AuthoredSeamEdge& second, float maximum_climb)
{
    const int64_t overlap_minimum
        = std::max(first.along_minimum, second.along_minimum);
    const int64_t overlap_maximum
        = std::min(first.along_maximum, second.along_maximum);
    if (overlap_minimum >= overlap_maximum) return false;

    const double difference_at_minimum
        = authored_seam_height_at(first, overlap_minimum)
        - authored_seam_height_at(second, overlap_minimum);
    const double difference_at_maximum
        = authored_seam_height_at(first, overlap_maximum)
        - authored_seam_height_at(second, overlap_maximum);
    if (difference_at_minimum * difference_at_maximum <= 0.0) return true;
    return std::min(std::abs(difference_at_minimum),
               std::abs(difference_at_maximum))
        <= static_cast<double>(maximum_climb);
}

uint32_t authored_component_root(
    nw::Vector<uint32_t>& parent, uint32_t component)
{
    while (parent[component] != component) {
        parent[component] = parent[parent[component]];
        component = parent[component];
    }
    return component;
}

bool build_authored_samples(const nw::nav::NavGeometry& surfaces,
    std::span<const uint8_t> surface_walkable, uint32_t width,
    uint32_t height, float maximum_climb,
    nw::Vector<AuthoredSample>& output,
    AuthoredSampleStats& stats)
{
    output.clear();
    stats = {};
    const size_t triangle_count = surfaces.triangle_count();
    if (!surfaces.valid() || surfaces.adjacency.size() != surfaces.indices.size()
        || triangle_count > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    nw::Vector<uint32_t> components(triangle_count, UINT32_MAX);
    nw::Vector<uint32_t> pending;
    for (size_t root = 0; root < triangle_count; ++root) {
        if (!walkable_triangle(surfaces, surface_walkable, root)) continue;
        ++stats.walkable_triangle_count;
        if (components[root] != UINT32_MAX) continue;
        if (stats.adjacency_component_count >= UINT32_MAX) return false;
        const uint32_t component
            = static_cast<uint32_t>(stats.adjacency_component_count++);
        components[root] = component;
        pending.push_back(static_cast<uint32_t>(root));
        while (!pending.empty()) {
            const uint32_t triangle = pending.back();
            pending.pop_back();
            for (size_t edge = 0; edge < 3; ++edge) {
                const int32_t neighbor
                    = surfaces.adjacency[static_cast<size_t>(triangle) * 3 + edge];
                if (neighbor < 0
                    || static_cast<size_t>(neighbor) >= triangle_count
                    || components[static_cast<size_t>(neighbor)] != UINT32_MAX
                    || !walkable_triangle(surfaces, surface_walkable,
                        static_cast<size_t>(neighbor))) {
                    continue;
                }
                components[static_cast<size_t>(neighbor)] = component;
                pending.push_back(static_cast<uint32_t>(neighbor));
            }
        }
    }

    nw::Vector<uint32_t> parent(stats.adjacency_component_count);
    for (uint32_t component = 0; component < parent.size(); ++component) {
        parent[component] = component;
    }
    nw::Vector<AuthoredSeamEdge> seam_edges;
    for (size_t source = 0; source < surfaces.indices.size(); ++source) {
        const int32_t neighbor = surfaces.adjacency[source];
        if (neighbor >= 0
            && static_cast<size_t>(neighbor) < triangle_count
            && walkable_triangle(surfaces, surface_walkable,
                static_cast<size_t>(neighbor))) {
            continue;
        }
        AuthoredSeamEdge edge;
        if (make_authored_seam_edge(surfaces, components, source, width,
                height, edge)) {
            seam_edges.push_back(edge);
        }
    }
    std::sort(seam_edges.begin(), seam_edges.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.seam != rhs.seam) return lhs.seam < rhs.seam;
            if (lhs.along_minimum != rhs.along_minimum) {
                return lhs.along_minimum < rhs.along_minimum;
            }
            if (lhs.along_maximum != rhs.along_maximum) {
                return lhs.along_maximum < rhs.along_maximum;
            }
            return lhs.owner < rhs.owner;
        });
    for (size_t begin = 0; begin < seam_edges.size();) {
        size_t end = begin + 1;
        while (end < seam_edges.size()
            && seam_edges[end].seam == seam_edges[begin].seam) {
            ++end;
        }
        for (size_t first = begin; first < end; ++first) {
            for (size_t second = first + 1;
                second < end
                && seam_edges[second].along_minimum
                    < seam_edges[first].along_maximum;
                ++second) {
                if (seam_edges[first].owner == seam_edges[second].owner
                    || !authored_seam_edges_connect(seam_edges[first],
                        seam_edges[second], maximum_climb)) {
                    continue;
                }
                uint32_t first_root = authored_component_root(
                    parent, seam_edges[first].component);
                uint32_t second_root = authored_component_root(
                    parent, seam_edges[second].component);
                if (first_root != second_root) parent[second_root] = first_root;
            }
        }
        begin = end;
    }

    nw::Vector<uint32_t> dense_components(parent.size(), UINT32_MAX);
    output.reserve(stats.walkable_triangle_count);
    for (size_t triangle = 0; triangle < triangle_count; ++triangle) {
        if (components[triangle] == UINT32_MAX) continue;
        const size_t offset = triangle * 3;
        const uint32_t ia = surfaces.indices[offset];
        const uint32_t ib = surfaces.indices[offset + 1];
        const uint32_t ic = surfaces.indices[offset + 2];
        if (ia >= surfaces.vertices.size() || ib >= surfaces.vertices.size()
            || ic >= surfaces.vertices.size()) {
            return false;
        }
        const uint32_t root
            = authored_component_root(parent, components[triangle]);
        if (dense_components[root] == UINT32_MAX) {
            if (stats.component_count >= UINT32_MAX) return false;
            dense_components[root]
                = static_cast<uint32_t>(stats.component_count++);
        }
        output.push_back({
            (surfaces.vertices[ia] + surfaces.vertices[ib]
                + surfaces.vertices[ic])
                / 3.0f,
            dense_components[root],
        });
    }
    stats.sampled_component_count = stats.component_count;
    return !output.empty();
}

bool remap_dense_owners(const nw::nav::NavGeometry& geometry,
    nw::Vector<uint32_t>& dense_owner, nw::Vector<uint8_t>& active)
{
    nw::Vector<uint32_t> owners = geometry.owner;
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    if (owners.size() > std::numeric_limits<uint32_t>::max()) return false;
    dense_owner.resize(geometry.owner.size());
    for (size_t row = 0; row < geometry.owner.size(); ++row) {
        const auto found = std::lower_bound(
            owners.begin(), owners.end(), geometry.owner[row]);
        if (found == owners.end() || *found != geometry.owner[row]) return false;
        dense_owner[row]
            = static_cast<uint32_t>(found - owners.begin());
    }
    active.assign(owners.size(), 1u);
    return true;
}

uint64_t count_cross_tile_links(const dtNavMesh& mesh)
{
    uint64_t result = 0;
    for (int tile_index = 0; tile_index < mesh.getMaxTiles(); ++tile_index) {
        const dtMeshTile* tile = mesh.getTile(tile_index);
        if (!tile || !tile->header) continue;
        const unsigned source_tile
            = mesh.decodePolyIdTile(mesh.getPolyRefBase(tile));
        for (int polygon = 0; polygon < tile->header->polyCount; ++polygon) {
            for (unsigned link = tile->polys[polygon].firstLink;
                link != DT_NULL_LINK; link = tile->links[link].next) {
                result += mesh.decodePolyIdTile(tile->links[link].ref)
                    != source_tile;
            }
        }
    }
    return result;
}

bool complete_path(dtNavMeshQuery& query, const dtQueryFilter& filter,
    dtPolyRef start_ref, const std::array<float, 3>& start,
    dtPolyRef end_ref, const std::array<float, 3>& end)
{
    std::array<dtPolyRef, k_max_path_polygons> corridor{};
    int corridor_count = 0;
    const dtStatus status = query.findPath(start_ref, end_ref, start.data(),
        end.data(), &filter, corridor.data(), &corridor_count,
        static_cast<int>(corridor.size()));
    return dtStatusSucceed(status) && corridor_count > 0
        && !dtStatusDetail(status, DT_PARTIAL_RESULT)
        && !dtStatusDetail(status, DT_BUFFER_TOO_SMALL)
        && corridor[static_cast<size_t>(corridor_count - 1)] == end_ref;
}

struct AuthoredLayers {
    uint64_t count = 0;
    float nearest_height = 0.0f;
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    float vertex_minimum = std::numeric_limits<float>::max();
    float vertex_maximum = std::numeric_limits<float>::lowest();
};

AuthoredLayers authored_layers_at(const nw::nav::NavGeometry& surfaces,
    std::span<const uint8_t> surface_walkable, const glm::vec2& point,
    float reference_height)
{
    AuthoredLayers result;
    float nearest_distance = std::numeric_limits<float>::max();
    for (size_t triangle = 0; triangle < surfaces.triangle_count(); ++triangle) {
        if (!walkable_triangle(surfaces, surface_walkable, triangle)) continue;
        const size_t offset = triangle * 3;
        const auto& a = surfaces.vertices[surfaces.indices[offset]];
        const auto& b = surfaces.vertices[surfaces.indices[offset + 1]];
        const auto& c = surfaces.vertices[surfaces.indices[offset + 2]];
        const float denominator = (b.y - c.y) * (a.x - c.x)
            + (c.x - b.x) * (a.y - c.y);
        if (std::abs(denominator) <= 1.0e-6f) continue;
        const float first = ((b.y - c.y) * (point.x - c.x)
                                + (c.x - b.x) * (point.y - c.y))
            / denominator;
        const float second = ((c.y - a.y) * (point.x - c.x)
                                 + (a.x - c.x) * (point.y - c.y))
            / denominator;
        const float third = 1.0f - first - second;
        if (first < -1.0e-4f || second < -1.0e-4f || third < -1.0e-4f) {
            continue;
        }
        const float height = first * a.z + second * b.z + third * c.z;
        result.minimum = std::min(result.minimum, height);
        result.maximum = std::max(result.maximum, height);
        result.vertex_minimum
            = std::min({result.vertex_minimum, a.z, b.z, c.z});
        result.vertex_maximum
            = std::max({result.vertex_maximum, a.z, b.z, c.z});
        const float distance = std::abs(height - reference_height);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            result.nearest_height = height;
        }
        ++result.count;
    }
    return result;
}

bool audit_nav_mesh(dtNavMesh& mesh,
    std::span<const AuthoredSample> samples,
    const nw::nav::NavGeometry& surfaces,
    std::span<const uint8_t> surface_walkable,
    const nw::nav::NavTileBuildConfig& config, AuditConfigStats& stats,
    nw::Vector<ProjectedSample>& projected)
{
    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
        dtAllocNavMeshQuery(), dtFreeNavMeshQuery};
    if (!query || dtStatusFailed(query->init(&mesh, 65535))) return false;
    dtQueryFilter filter;
    filter.setIncludeFlags(nw::nav::nav_walkable_flag);
    const std::array<float, 3> extents{
        config.cell_size * 2.0f,
        config.agent_max_climb + config.cell_height,
        config.cell_size * 2.0f,
    };

    stats.projection_count = samples.size();
    projected.clear();
    projected.reserve(samples.size());
    uint32_t component_count = 0;
    for (const auto& sample : samples) {
        const auto point = to_detour(sample.position);
        std::array<float, 3> nearest{};
        dtPolyRef polygon = 0;
        if (dtStatusFailed(query->findNearestPoly(point.data(), extents.data(),
                &filter, &polygon, nearest.data()))
            || polygon == 0) {
            ++stats.projection_failure_count;
            continue;
        }
        const float delta_x = nearest[0] - point[0];
        const float delta_z = nearest[2] - point[2];
        const float horizontal = std::sqrt(
            delta_x * delta_x + delta_z * delta_z);
        const auto authored_layers = authored_layers_at(surfaces,
            surface_walkable, {nearest[0], nearest[2]}, sample.position.z);
        if (authored_layers.count == 0) {
            ++stats.projection_failure_count;
            continue;
        }
        const float surface_error
            = std::abs(nearest[1] - authored_layers.nearest_height);
        stats.maximum_horizontal_projection
            = std::max(stats.maximum_horizontal_projection, horizontal);
        if (surface_error > stats.maximum_surface_error) {
            stats.maximum_surface_error = surface_error;
            stats.maximum_surface_error_input
                = {nearest[0], nearest[2], authored_layers.nearest_height};
            stats.maximum_surface_error_output
                = {nearest[0], nearest[2], nearest[1]};
            stats.authored_layer_count_at_maximum_error
                = authored_layers.count;
            stats.authored_layer_minimum_at_maximum_error
                = authored_layers.minimum;
            stats.authored_layer_maximum_at_maximum_error
                = authored_layers.maximum;
            stats.authored_triangle_vertex_minimum_at_maximum_error
                = authored_layers.vertex_minimum;
            stats.authored_triangle_vertex_maximum_at_maximum_error
                = authored_layers.vertex_maximum;
        }
        stats.surface_error_failure_count
            += surface_error > config.cell_height + 1.0e-4f;
        projected.push_back({nearest, polygon, sample.component});
        component_count = std::max(component_count, sample.component + 1u);
    }

    nw::Vector<size_t> representatives(component_count, SIZE_MAX);
    for (size_t row = 0; row < projected.size(); ++row) {
        const uint32_t component = projected[row].component;
        if (representatives[component] == SIZE_MAX) {
            representatives[component] = row;
        }
    }
    for (const auto& sample : projected) {
        const size_t representative = representatives[sample.component];
        if (representative == SIZE_MAX) continue;
        const auto& start = projected[representative];
        ++stats.route_count;
        stats.lost_route_count += !complete_path(*query, filter,
            start.polygon, start.position, sample.polygon, sample.position);
    }

    nw::Vector<size_t> sampled_representatives;
    sampled_representatives.reserve(representatives.size());
    for (size_t representative : representatives) {
        if (representative != SIZE_MAX) {
            sampled_representatives.push_back(representative);
        }
    }
    const uint64_t sampled_component_count = sampled_representatives.size();
    const uint64_t component_pairs = sampled_component_count < 2
        ? 0
        : sampled_component_count * (sampled_component_count - 1) / 2;
    if (component_pairs > k_max_component_pair_audits) return false;
    stats.disconnected_component_pair_count = component_pairs;
    for (size_t first = 0; first < sampled_representatives.size(); ++first) {
        const auto& start = projected[sampled_representatives[first]];
        for (size_t second = first + 1;
            second < sampled_representatives.size(); ++second) {
            const auto& end = projected[sampled_representatives[second]];
            stats.unexpected_route_count += complete_path(*query, filter,
                start.polygon, start.position, end.polygon, end.position);
        }
    }
    return true;
}

bool audit_polygon_graph_routes(const nw::nav::NavGeometry& surfaces,
    const nw::nav::NavGeometry& obstacles,
    std::span<const uint32_t> obstacle_owner,
    std::span<const uint8_t> obstacle_active,
    std::span<const uint8_t> surface_walkable,
    std::span<const ProjectedSample> samples, uint32_t width, uint32_t height,
    const nw::nav::NavTileBuildConfig& config, AuditConfigStats& stats)
{
    if (obstacle_active.size() > UINT32_MAX) return false;
    nw::nav::NavAreaBuildSource source;
    source.surface_vertices = surfaces.vertices;
    source.surface_indices = surfaces.indices;
    source.surface_ids = surfaces.surface;
    source.surface_walkable.assign(
        surface_walkable.begin(), surface_walkable.end());
    source.obstacle_vertices = obstacles.vertices;
    source.obstacle_indices = obstacles.indices;
    source.obstacle_surface_ids = obstacles.surface;
    source.obstacle_owner.assign(obstacle_owner.begin(), obstacle_owner.end());
    source.width = width;
    source.height = height;
    source.obstacle_state_count
        = static_cast<uint32_t>(obstacle_active.size());

    nw::nav::NavWorldState world;
    nw::nav::NavTiledWorldBuildStats build;
    if (nw::nav::build_tiled_nav_world(
            source, obstacle_active, config, world, build)
        != nw::nav::NavStatus::ok) {
        return false;
    }
    stats.polygon_graph_bytes = build.polygon_graph_bytes;
    stats.polygon_graph_build_nanoseconds
        = build.polygon_graph_build_nanoseconds;

    uint32_t component_count = 0;
    for (const auto& sample : samples) {
        if (sample.component == UINT32_MAX) return false;
        component_count = std::max(component_count, sample.component + 1u);
    }
    nw::Vector<size_t> representatives(component_count, SIZE_MAX);
    for (size_t row = 0; row < samples.size(); ++row) {
        if (representatives[samples[row].component] == SIZE_MAX) {
            representatives[samples[row].component] = row;
        }
    }

    nw::Vector<nw::nav::NavPathRequest> requests;
    requests.reserve(samples.size());
    for (const auto& sample : samples) {
        const size_t representative = representatives[sample.component];
        if (representative == SIZE_MAX) return false;
        const auto& start = samples[representative].position;
        requests.push_back({
            {start[0], start[2], start[1]},
            {sample.position[0], sample.position[2], sample.position[1]},
        });
    }
    nw::Vector<nw::nav::NavPathResult> results(requests.size());
    nw::Vector<glm::vec3> corners;
    corners.reserve(requests.size() * nw::nav::maximum_nav_path_corners);
    (void)nw::nav::find_nav_paths(world, requests, corners, results);
    stats.polygon_graph_route_count = results.size();
    for (size_t row = 0; row < results.size(); ++row) {
        if (results[row].status == nw::nav::NavStatus::ok) continue;
        stats.polygon_graph_clamped_route_count
            += results[row].status == nw::nav::NavStatus::clamped;
        ++stats.polygon_graph_lost_route_count;
        std::cout << "polygon_graph_route_failure=" << row
                  << " status=" << static_cast<int>(results[row].status)
                  << " start=" << requests[row].start.x << ','
                  << requests[row].start.y << ',' << requests[row].start.z
                  << " end=" << requests[row].end.x << ','
                  << requests[row].end.y << ',' << requests[row].end.z
                  << '\n';
    }

    requests.clear();
    for (size_t first = 0; first < representatives.size(); ++first) {
        if (representatives[first] == SIZE_MAX) continue;
        for (size_t second = first + 1; second < representatives.size();
            ++second) {
            if (representatives[second] == SIZE_MAX) continue;
            if (requests.size() == k_max_component_pair_audits) return false;
            const auto& start = samples[representatives[first]].position;
            const auto& end = samples[representatives[second]].position;
            requests.push_back({
                {start[0], start[2], start[1]},
                {end[0], end[2], end[1]},
            });
        }
    }
    results.assign(requests.size(), {});
    corners.clear();
    corners.reserve(requests.size() * nw::nav::maximum_nav_path_corners);
    (void)nw::nav::find_nav_paths(world, requests, corners, results);
    for (const auto& result : results) {
        stats.polygon_graph_unexpected_route_count
            += result.status == nw::nav::NavStatus::ok;
    }
    return true;
}

bool build_audit_config(const nw::nav::NavGeometry& surfaces,
    const nw::nav::NavGeometry& obstacles,
    std::span<const uint32_t> obstacle_owner,
    std::span<const uint8_t> obstacle_active,
    std::span<const uint8_t> surface_walkable,
    std::span<const AuthoredSample> samples, uint32_t width, uint32_t height,
    const nw::nav::NavTileBuildConfig& config, AuditConfigStats& stats)
{
    stats = {};
    stats.tile_count = static_cast<uint64_t>(width) * height;
    if (stats.tile_count == 0
        || stats.tile_count
            > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const float border
        = static_cast<float>(config.erosion_cells + 3) * config.cell_size;
    nw::nav::NavTileTriangleRanges surface_ranges;
    nw::nav::NavTileTriangleRanges obstacle_ranges;
    if (nw::nav::build_nav_tile_triangle_ranges(surfaces.vertices,
            surfaces.indices, width, height, border, surface_ranges)
                .status
            != nw::nav::NavStatus::ok
        || nw::nav::build_nav_tile_triangle_ranges(obstacles.vertices,
               obstacles.indices, width, height, border, obstacle_ranges)
                .status
            != nw::nav::NavStatus::ok) {
        return false;
    }

    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{
        dtAllocNavMesh(), dtFreeNavMesh};
    if (!mesh) return false;
    dtNavMeshParams params{};
    params.tileWidth = nw::nav::nav_tile_size;
    params.tileHeight = nw::nav::nav_tile_size;
    params.maxTiles = static_cast<int>(stats.tile_count);
    params.maxPolys = 4096;
    if (dtStatusFailed(mesh->init(&params))) return false;

    const auto started = std::chrono::steady_clock::now();
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const nw::nav::NavTileCoord tile{x, y};
            const nw::nav::NavTileBuildInput input{
                .surface_vertices = surfaces.vertices,
                .surface_indices = surfaces.indices,
                .surface_ids = surfaces.surface,
                .surface_triangles = surface_ranges.tile(tile, width),
                .surface_walkable = surface_walkable,
                .obstacle_vertices = obstacles.vertices,
                .obstacle_indices = obstacles.indices,
                .obstacle_surface_ids = obstacles.surface,
                .obstacle_owner = obstacle_owner,
                .obstacle_triangles = obstacle_ranges.tile(tile, width),
                .obstacle_active = obstacle_active,
                .tile = tile,
            };
            nw::nav::NavTileData tile_data;
            const auto build
                = nw::nav::build_nav_tile_data(input, config, tile_data);
            if (build.status != nw::nav::NavStatus::ok) {
                ++stats.rejected_tile_count;
                continue;
            }
            stats.polygon_count += build.polygon_count;
            stats.payload_bytes += build.payload_bytes;
            if (tile_data.empty()) {
                ++stats.empty_tile_count;
                continue;
            }
            const int payload_size = tile_data.size();
            unsigned char* payload = tile_data.release();
            if (dtStatusFailed(mesh->addTile(payload, payload_size,
                    DT_TILE_FREE_DATA, 0, nullptr))) {
                dtFree(payload);
                ++stats.rejected_tile_count;
                continue;
            }
            ++stats.built_tile_count;
        }
    }
    stats.build_nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    stats.cross_tile_links = count_cross_tile_links(*mesh);
    nw::Vector<ProjectedSample> projected;
    if (!audit_nav_mesh(*mesh, samples, surfaces, surface_walkable, config,
            stats, projected)) {
        return false;
    }
    if (!audit_polygon_graph_routes(surfaces, obstacles, obstacle_owner,
            obstacle_active, surface_walkable, projected, width, height,
            config, stats)) {
        return false;
    }
    stats.passed = stats.rejected_tile_count == 0
        && stats.projection_failure_count == 0
        && stats.surface_error_failure_count == 0
        && stats.lost_route_count == 0
        && stats.polygon_graph_lost_route_count == 0
        && stats.unexpected_route_count == 0
        && stats.polygon_graph_unexpected_route_count == 0;
    return true;
}

} // namespace

int run_nav_audit_command(std::string_view area_resref,
    std::string_view module_path, std::string_view user_path)
{
    if (area_resref.empty() || !init_kernel_services(module_path, user_path)) {
        return 1;
    }
    const auto shutdown = [] { nw::kernel::services().shutdown(); };
    nw::ObjectManager::AreaLoadProfile profile;
    auto* area = nw::kernel::objects().make_area(
        nw::Resref{area_resref}, &profile);
    if (!area || !area->instantiate() || !area->tileset
        || area->width <= 0 || area->height <= 0) {
        LOG_F(ERROR, "Unable to instantiate navigation area: {}", area_resref);
        if (area) nw::kernel::objects().destroy(area->handle());
        shutdown();
        return 1;
    }

    nw::nav::NavGeometry surfaces;
    nw::nav::NavGeometry obstacles;
    const auto surface_stats = nw::nav::build_area_tile_nav_geometry(
        *area, nw::kernel::resman(), surfaces);
    const auto obstacle_stats = nw::nav::build_area_object_nav_geometry(
        *area, nw::kernel::resman(), obstacles);
    const auto* surface_table = nw::kernel::twodas().get("surfacemat");
    nw::Vector<uint8_t> surface_walkable;
    nw::Vector<uint32_t> obstacle_owner;
    nw::Vector<uint8_t> obstacle_active;
    if (!surfaces.valid() || !obstacles.valid() || !surface_table
        || nw::nav::build_nav_surface_walkability(
               *surface_table, surface_walkable)
                .walkable_count
            == 0
        || !remap_dense_owners(
            obstacles, obstacle_owner, obstacle_active)) {
        LOG_F(ERROR, "Navigation input extraction failed: {}", area_resref);
        nw::kernel::objects().destroy(area->handle());
        shutdown();
        return 1;
    }

    const uint32_t width = static_cast<uint32_t>(area->width);
    const uint32_t height = static_cast<uint32_t>(area->height);
    nw::Vector<AuthoredSample> samples;
    AuthoredSampleStats sample_stats;
    const nw::nav::NavTileBuildConfig nav_profile;
    if (!build_authored_samples(surfaces, surface_walkable, width, height,
            nav_profile.agent_max_climb, samples, sample_stats)) {
        LOG_F(ERROR, "Authored navigation sampling failed: {}", area_resref);
        nw::kernel::objects().destroy(area->handle());
        shutdown();
        return 1;
    }
    // Cell-size selection measures the authored WOK topology independently of
    // volatile object and door states. Obstacle rasterization has a focused
    // synthetic gate; active-state area routes are added with door links.
    nw::Vector<uint8_t> inactive_obstacles(obstacle_active.size(), 0u);

    std::cout << "area=" << area_resref << " width=" << area->width
              << " height=" << area->height
              << " surface_triangles=" << surfaces.triangle_count()
              << " obstacle_triangles=" << obstacles.triangle_count()
              << " wok_tiles=" << surface_stats.wok_tile_count
              << " fallback_tiles=" << surface_stats.fallback_tile_count
              << " placeables=" << obstacle_stats.placeable_count
              << " doors=" << obstacle_stats.door_count
              << " walkable_triangles="
              << sample_stats.walkable_triangle_count
              << " adjacency_components="
              << sample_stats.adjacency_component_count
              << " authored_components=" << sample_stats.component_count
              << " sampled_components="
              << sample_stats.sampled_component_count
              << " radius_valid_samples=" << samples.size()
              << " rejected_samples=" << sample_stats.rejected_sample_count
              << '\n';
    struct ComponentBounds {
        glm::vec3 minimum{std::numeric_limits<float>::max()};
        glm::vec3 maximum{std::numeric_limits<float>::lowest()};
        uint64_t sample_count = 0;
    };
    nw::Vector<ComponentBounds> component_bounds(sample_stats.component_count);
    for (const auto& sample : samples) {
        auto& bounds = component_bounds[sample.component];
        bounds.minimum.x = std::min(bounds.minimum.x, sample.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, sample.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, sample.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, sample.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, sample.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, sample.position.z);
        ++bounds.sample_count;
    }
    for (size_t component = 0; component < component_bounds.size(); ++component) {
        const auto& bounds = component_bounds[component];
        std::cout << "authored_component=" << component
                  << " samples=" << bounds.sample_count << " minimum="
                  << bounds.minimum.x << ',' << bounds.minimum.y << ','
                  << bounds.minimum.z << " maximum=" << bounds.maximum.x
                  << ',' << bounds.maximum.y << ',' << bounds.maximum.z
                  << '\n';
    }

    constexpr std::array cell_sizes{0.25f, 0.125f, 0.0625f};
    constexpr std::array cell_heights{0.10f, 0.05f};
    constexpr std::array detail_sample_distances{0.9f, 1.0f, 3.0f, 6.0f};
    bool success = true;
    bool any_config_passed = false;
    AuditSelection selection;
    for (float cell_size : cell_sizes) {
        for (float cell_height : cell_heights) {
            AuditSelection pair_selection;
            for (float detail_sample_distance : detail_sample_distances) {
                nw::nav::NavTileBuildConfig config;
                config.cell_size = cell_size;
                config.cell_height = cell_height;
                config.erosion_cells = static_cast<uint16_t>(
                    std::ceil(0.1f / cell_size));
                config.detail_sample_distance = detail_sample_distance;
                AuditConfigStats stats;
                const bool audited = build_audit_config(surfaces, obstacles,
                    obstacle_owner, inactive_obstacles, surface_walkable,
                    samples, width, height, config, stats);
                success = success && audited;
                any_config_passed = any_config_passed || stats.passed;
                if (stats.passed) {
                    pair_selection = {config, stats, true};
                }
                std::cout << "cell_size=" << cell_size
                          << " cell_height=" << cell_height
                          << " erosion_cells=" << config.erosion_cells
                          << " detail_sample_distance="
                          << config.detail_sample_distance
                          << " tiles=" << stats.tile_count
                          << " built=" << stats.built_tile_count
                          << " empty=" << stats.empty_tile_count
                          << " rejected=" << stats.rejected_tile_count
                          << " polygons=" << stats.polygon_count
                          << " payload_bytes=" << stats.payload_bytes
                          << " cross_tile_links=" << stats.cross_tile_links
                          << " projections=" << stats.projection_count
                          << " projection_failures="
                          << stats.projection_failure_count
                          << " maximum_horizontal_projection="
                          << stats.maximum_horizontal_projection
                          << " maximum_surface_error="
                          << stats.maximum_surface_error
                          << " maximum_surface_error_input="
                          << stats.maximum_surface_error_input.x << ','
                          << stats.maximum_surface_error_input.y << ','
                          << stats.maximum_surface_error_input.z
                          << " maximum_surface_error_output="
                          << stats.maximum_surface_error_output.x << ','
                          << stats.maximum_surface_error_output.y << ','
                          << stats.maximum_surface_error_output.z
                          << " authored_layers_at_maximum_error="
                          << stats.authored_layer_count_at_maximum_error
                          << " authored_layer_range_at_maximum_error="
                          << stats.authored_layer_minimum_at_maximum_error << ','
                          << stats.authored_layer_maximum_at_maximum_error
                          << " authored_triangle_vertex_range_at_maximum_error="
                          << stats.authored_triangle_vertex_minimum_at_maximum_error
                          << ','
                          << stats.authored_triangle_vertex_maximum_at_maximum_error
                          << " surface_error_failures="
                          << stats.surface_error_failure_count
                          << " routes=" << stats.route_count
                          << " lost_routes=" << stats.lost_route_count
                          << " polygon_graph_routes="
                          << stats.polygon_graph_route_count
                          << " polygon_graph_clamped_routes="
                          << stats.polygon_graph_clamped_route_count
                          << " polygon_graph_lost_routes="
                          << stats.polygon_graph_lost_route_count
                          << " disconnected_component_pairs="
                          << stats.disconnected_component_pair_count
                          << " unexpected_routes="
                          << stats.unexpected_route_count
                          << " polygon_graph_unexpected_routes="
                          << stats.polygon_graph_unexpected_route_count
                          << " polygon_graph_bytes="
                          << stats.polygon_graph_bytes
                          << " polygon_graph_build_ms="
                          << static_cast<double>(
                                 stats.polygon_graph_build_nanoseconds)
                        / 1.0e6
                          << " build_ms="
                          << static_cast<double>(stats.build_nanoseconds) / 1.0e6
                          << " passed=" << stats.passed
                          << '\n';
            }
            if (!selection.found && pair_selection.found) {
                selection = pair_selection;
            }
        }
    }

    if (selection.found) {
        std::cout << "selected_cell_size=" << selection.config.cell_size
                  << " selected_cell_height=" << selection.config.cell_height
                  << " selected_erosion_cells="
                  << selection.config.erosion_cells
                  << " selected_detail_sample_distance="
                  << selection.config.detail_sample_distance
                  << " selected_build_ms="
                  << static_cast<double>(selection.stats.build_nanoseconds)
                / 1.0e6
                  << " selection_rule=coarsest_cells_largest_passing_detail"
                  << '\n';
    } else {
        std::cout << "selected_config=none\n";
    }

    nw::nav::NavTileBuildConfig committed_config;
    committed_config.erosion_cells = static_cast<uint16_t>(
        std::ceil(0.1f / committed_config.cell_size));
    const auto grid_stats
        = build_synthetic_grid(32, 32, committed_config);
    std::cout << "synthetic_grid=32x32"
              << " cell_size=" << committed_config.cell_size
              << " cell_height=" << committed_config.cell_height
              << " erosion_cells=" << committed_config.erosion_cells
              << " detail_sample_distance="
              << committed_config.detail_sample_distance
              << " tiles=" << grid_stats.tile_count
              << " built=" << grid_stats.built_tile_count
              << " empty=" << grid_stats.empty_tile_count
              << " rejected=" << grid_stats.rejected_tile_count
              << " polygons=" << grid_stats.polygon_count
              << " payload_bytes=" << grid_stats.payload_bytes
              << " polygon_graph_bytes="
              << grid_stats.polygon_graph_bytes
              << " tile_build_ms="
              << static_cast<double>(grid_stats.tile_build_nanoseconds) / 1.0e6
              << " polygon_graph_build_ms="
              << static_cast<double>(
                     grid_stats.polygon_graph_build_nanoseconds)
            / 1.0e6
              << " total_ms="
              << static_cast<double>(grid_stats.total_nanoseconds) / 1.0e6
              << '\n';

    nw::kernel::objects().destroy(area->handle());
    shutdown();
    return success && any_config_passed ? 0 : 1;
}

} // namespace mudl
