#include "area_render_scene.hpp"

#include "preview_model_draws.hpp"
#include "preview_scene.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/model_asset.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace nw::render::viewer {
namespace {

static constexpr uint32_t kInvalidChunkId = std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kSortedVisibleStaticSurfaceThreshold = 4096;
static constexpr uint32_t kSortedVisibleStaticSurfaceMinimumSceneCoveragePercent = 50;
static constexpr float kAreaTileSelectionOutlineHeight = 1.0f;

uint32_t saturating_count(size_t value)
{
    return static_cast<uint32_t>(std::min<size_t>(value, std::numeric_limits<uint32_t>::max()));
}

bool finite_vec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_ordered_bounds(const nw::render::Bounds& bounds) noexcept
{
    return finite_vec3(bounds.min) && finite_vec3(bounds.max)
        && bounds.min.x <= bounds.max.x
        && bounds.min.y <= bounds.max.y
        && bounds.min.z <= bounds.max.z;
}

bool object_matches_record_kind(AreaRenderRecordKind kind, nw::ObjectHandle object) noexcept
{
    switch (kind) {
    case AreaRenderRecordKind::creature:
        return object.type == nw::ObjectType::creature;
    case AreaRenderRecordKind::door:
        return object.type == nw::ObjectType::door;
    case AreaRenderRecordKind::item:
        return object.type == nw::ObjectType::item;
    case AreaRenderRecordKind::placeable:
        return object.type == nw::ObjectType::placeable;
    case AreaRenderRecordKind::waypoint:
        return object.type == nw::ObjectType::waypoint;
    case AreaRenderRecordKind::tile:
    case AreaRenderRecordKind::unknown:
        return false;
    }
    return false;
}

std::optional<float> ray_bounds_intersection(
    const AreaObjectRay& ray, const nw::render::Bounds& bounds) noexcept
{
    if (!finite_ordered_bounds(bounds)) {
        return std::nullopt;
    }

    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::infinity();
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        const float minimum = bounds.min[axis];
        const float maximum = bounds.max[axis];
        if (std::abs(direction) <= 1.0e-8f) {
            if (origin < minimum || origin > maximum) {
                return std::nullopt;
            }
            continue;
        }

        const float inverse = 1.0f / direction;
        float near_distance = (minimum - origin) * inverse;
        float far_distance = (maximum - origin) * inverse;
        if (near_distance > far_distance) {
            std::swap(near_distance, far_distance);
        }
        t_min = std::max(t_min, near_distance);
        t_max = std::min(t_max, far_distance);
        if (t_max < t_min) {
            return std::nullopt;
        }
    }
    return t_min;
}

std::optional<float> ray_triangle_intersection(
    const AreaObjectRay& ray, const AreaSurfaceTriangle& triangle) noexcept
{
    constexpr float kEpsilon = 1.0e-7f;
    const glm::vec3 edge0 = triangle.v1 - triangle.v0;
    const glm::vec3 edge1 = triangle.v2 - triangle.v0;
    const glm::vec3 perpendicular = glm::cross(ray.direction, edge1);
    const float determinant = glm::dot(edge0, perpendicular);
    if (!std::isfinite(determinant) || std::abs(determinant) <= kEpsilon) {
        return std::nullopt;
    }

    const float inverse_determinant = 1.0f / determinant;
    const glm::vec3 origin_delta = ray.origin - triangle.v0;
    const float u = glm::dot(origin_delta, perpendicular) * inverse_determinant;
    if (!std::isfinite(u) || u < 0.0f || u > 1.0f) {
        return std::nullopt;
    }

    const glm::vec3 cross = glm::cross(origin_delta, edge0);
    const float v = glm::dot(ray.direction, cross) * inverse_determinant;
    if (!std::isfinite(v) || v < 0.0f || u + v > 1.0f) {
        return std::nullopt;
    }

    const float distance = glm::dot(edge1, cross) * inverse_determinant;
    return std::isfinite(distance) && distance >= 0.0f
        ? std::optional<float>{distance}
        : std::nullopt;
}

bool upward_surface_normal(
    const AreaSurfaceTriangle& triangle, glm::vec3& normal) noexcept
{
    constexpr float kMinimumUpwardRatio = 1.0e-4f;
    const glm::vec3 cross = glm::cross(
        triangle.v1 - triangle.v0, triangle.v2 - triangle.v0);
    const float length_squared = glm::dot(cross, cross);
    if (!finite_vec3(cross) || !std::isfinite(length_squared)
        || length_squared <= 1.0e-12f) {
        return false;
    }

    const float length = std::sqrt(length_squared);
    if (cross.z <= length * kMinimumUpwardRatio) {
        return false;
    }
    normal = cross / length;
    return finite_vec3(normal);
}

bool valid_surface_protocol(
    std::span<const AreaSurfaceRange> ranges,
    std::span<const AreaSurfaceTriangle> triangles) noexcept
{
    if (ranges.size() > std::numeric_limits<uint32_t>::max()
        || triangles.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    for (const auto& range : ranges) {
        const uint64_t triangle_end = static_cast<uint64_t>(range.first_triangle)
            + static_cast<uint64_t>(range.triangle_count);
        if (range.triangle_count == 0
            || triangle_end > triangles.size()
            || !finite_ordered_bounds(range.bounds)) {
            return false;
        }
    }
    return true;
}

void expand_bounds_with_point(
    nw::render::Bounds& bounds, const glm::vec3& point, bool& initialized) noexcept
{
    if (!initialized) {
        bounds = {.min = point, .max = point};
        initialized = true;
        return;
    }
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

template <typename Vertex, typename Index>
bool append_upward_surface_triangles(
    std::span<const Vertex> vertices,
    std::span<const Index> indices,
    const glm::mat4& world,
    std::vector<AreaSurfaceTriangle>& triangles,
    nw::render::Bounds& bounds,
    bool& has_bounds)
{
    const size_t triangle_count = indices.size() / 3u;
    for (size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const size_t i0 = static_cast<size_t>(indices[triangle_index * 3u]);
        const size_t i1 = static_cast<size_t>(indices[triangle_index * 3u + 1u]);
        const size_t i2 = static_cast<size_t>(indices[triangle_index * 3u + 2u]);
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const AreaSurfaceTriangle triangle{
            .v0 = glm::vec3(world * glm::vec4(vertices[i0].position, 1.0f)),
            .v1 = glm::vec3(world * glm::vec4(vertices[i1].position, 1.0f)),
            .v2 = glm::vec3(world * glm::vec4(vertices[i2].position, 1.0f)),
        };
        glm::vec3 normal{0.0f};
        if (!finite_vec3(triangle.v0)
            || !finite_vec3(triangle.v1)
            || !finite_vec3(triangle.v2)
            || !upward_surface_normal(triangle, normal)) {
            continue;
        }
        if (triangles.size() == std::numeric_limits<uint32_t>::max()) {
            return false;
        }

        triangles.push_back(triangle);
        expand_bounds_with_point(bounds, triangle.v0, has_bounds);
        expand_bounds_with_point(bounds, triangle.v1, has_bounds);
        expand_bounds_with_point(bounds, triangle.v2, has_bounds);
    }
    return true;
}

bool append_tile_surface_range(
    const nw::render::RenderModel& model,
    const nw::render::ModelInstance* instance,
    const glm::mat4& root,
    std::vector<AreaSurfaceRange>& ranges,
    std::vector<AreaSurfaceTriangle>& triangles)
{
    if (triangles.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    const size_t triangle_begin = triangles.size();
    nw::render::Bounds bounds{};
    bool has_bounds = false;
    for (const auto& primitive : model.primitives) {
        if (primitive.skinned || primitive.vertex_count == 0u || primitive.index_count < 3u) {
            continue;
        }
        if (!primitive.vertices.valid() || !primitive.indices.valid()) {
            triangles.resize(triangle_begin);
            return false;
        }

        const auto* vertices = static_cast<const nw::render::Vertex*>(nw::gfx::map_buffer(primitive.vertices));
        const auto* indices = nw::gfx::map_buffer(primitive.indices);
        if (!vertices || !indices) {
            if (vertices) {
                nw::gfx::unmap_buffer(primitive.vertices);
            }
            if (indices) {
                nw::gfx::unmap_buffer(primitive.indices);
            }
            triangles.resize(triangle_begin);
            return false;
        }

        const glm::mat4 world = nw::render::model_instance_primitive_world_transform(
            instance, root, primitive);
        bool appended = false;
        if (primitive.index_stride == sizeof(uint16_t)) {
            appended = append_upward_surface_triangles(
                std::span{vertices, primitive.vertex_count},
                std::span{static_cast<const uint16_t*>(indices), primitive.index_count},
                world,
                triangles,
                bounds,
                has_bounds);
        } else if (primitive.index_stride == sizeof(uint32_t)) {
            appended = append_upward_surface_triangles(
                std::span{vertices, primitive.vertex_count},
                std::span{static_cast<const uint32_t*>(indices), primitive.index_count},
                world,
                triangles,
                bounds,
                has_bounds);
        }
        nw::gfx::unmap_buffer(primitive.vertices);
        nw::gfx::unmap_buffer(primitive.indices);
        if (!appended) {
            triangles.resize(triangle_begin);
            return false;
        }
    }

    if (!has_bounds || triangles.size() == triangle_begin) {
        return true;
    }
    ranges.push_back(AreaSurfaceRange{
        .bounds = bounds,
        .first_triangle = static_cast<uint32_t>(triangle_begin),
        .triangle_count = static_cast<uint32_t>(triangles.size() - triangle_begin),
    });
    return true;
}

const AreaRenderSourceInfo& source_info_for_render_model(const PreviewScene& scene, size_t index)
{
    static const AreaRenderSourceInfo kDefaultInfo{};
    if (index < scene.static_area_model_info.size()) {
        return scene.static_area_model_info[index];
    }
    return kDefaultInfo;
}

bool has_opaque_cutout_pass(uint8_t pass_mask) noexcept
{
    return (pass_mask & 0x1u) != 0;
}

bool has_water_pass(uint8_t pass_mask) noexcept
{
    return (pass_mask & 0x2u) != 0;
}

bool has_transparent_pass(uint8_t pass_mask) noexcept
{
    return (pass_mask & 0x4u) != 0;
}

bool has_flag(uint8_t flags, AreaRenderScene::RecordFlag flag) noexcept
{
    return (flags & static_cast<uint8_t>(flag)) != 0;
}

void set_flag(uint8_t& flags, AreaRenderScene::RecordFlag flag, bool enabled) noexcept
{
    if (enabled) {
        flags |= static_cast<uint8_t>(flag);
    } else {
        flags &= static_cast<uint8_t>(~static_cast<uint8_t>(flag));
    }
}

uint8_t render_model_pass_mask(const nw::render::RenderModel& model) noexcept
{
    uint8_t result = 0;
    for (const auto& primitive : model.primitives) {
        if (primitive.index_count == 0 || primitive.material >= model.materials.size()) {
            continue;
        }
        switch (model.materials[primitive.material].alpha_mode) {
        case nw::render::MaterialMode::opaque:
        case nw::render::MaterialMode::cutout:
            result |= 0x1u;
            break;
        case nw::render::MaterialMode::water:
            result |= 0x2u;
            break;
        case nw::render::MaterialMode::transparent:
            result |= 0x4u;
            break;
        }
    }
    return result;
}

bool render_model_casts_shadow(const nw::render::RenderModel& model) noexcept
{
    const auto summary = model.shadow.valid
        ? model.shadow
        : nw::render::summarize_render_model_shadows(model);
    return summary.casts_shadow;
}

void collect_local_light_indices_for_bounds(
    std::vector<uint32_t>& indices,
    std::span<const nw::render::LocalLight> lights,
    const nw::render::Bounds& bounds)
{
    for (size_t light_index = 0; light_index < lights.size(); ++light_index) {
        if (light_index > std::numeric_limits<uint32_t>::max()) {
            break;
        }

        const auto& light = lights[light_index];
        if (light.radius <= 1.0e-4f) {
            continue;
        }

        const glm::vec3 closest = glm::clamp(light.position, bounds.min, bounds.max);
        glm::vec3 delta = light.position - closest;
        if (light.contribution == nw::render::LocalLightContribution::ambient) {
            delta.z *= std::clamp(light.vertical_scale, 0.0f, 1.0f);
        }
        if (glm::dot(delta, delta) < light.radius * light.radius) {
            indices.push_back(static_cast<uint32_t>(light_index));
        }
    }
}

std::span<const uint32_t> prepared_surface_index_span(
    std::span<const uint32_t> indices,
    const std::array<uint32_t, 5>& offsets,
    uint32_t first_pass_index,
    uint32_t one_past_last_pass_index) noexcept
{
    if (first_pass_index >= one_past_last_pass_index || one_past_last_pass_index >= offsets.size()) {
        return {};
    }
    const uint32_t begin = offsets[first_pass_index];
    const uint32_t end = offsets[one_past_last_pass_index];
    if (end <= begin || end > indices.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        indices.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

bool prepared_surface_index_less(
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    uint32_t lhs,
    uint32_t rhs) noexcept
{
    const bool lhs_valid = lhs < surfaces.size();
    const bool rhs_valid = rhs < surfaces.size();
    if (lhs_valid != rhs_valid) {
        return lhs_valid;
    }
    if (!lhs_valid) {
        return lhs < rhs;
    }
    const auto& lhs_surface = surfaces[lhs];
    const auto& rhs_surface = surfaces[rhs];
    if (nw::render::prepared_model_surface_sort_less(lhs_surface, rhs_surface)) {
        return true;
    }
    if (nw::render::prepared_model_surface_sort_less(rhs_surface, lhs_surface)) {
        return false;
    }
    return lhs < rhs;
}

void sort_prepared_surface_indices(
    std::vector<uint32_t>& indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces)
{
    std::sort(indices.begin(), indices.end(), [&](uint32_t lhs, uint32_t rhs) noexcept {
        return prepared_surface_index_less(surfaces, lhs, rhs);
    });
}

void rebuild_prepared_surface_pass_offsets(
    std::array<uint32_t, 5>& offsets,
    std::span<const uint32_t> indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces) noexcept
{
    offsets.fill(saturating_count(indices.size()));
    uint32_t cursor = 0;
    for (uint32_t pass_index = 0; pass_index < 4u; ++pass_index) {
        offsets[pass_index] = cursor;
        while (cursor < indices.size()) {
            const uint32_t surface_index = indices[cursor];
            if (surface_index >= surfaces.size()
                || nw::render::prepared_model_surface_pass_index(surfaces[surface_index].material_mode)
                    != pass_index) {
                break;
            }
            ++cursor;
        }
    }
    offsets[4] = cursor;
}

void rebuild_prepared_surface_indices(
    std::vector<uint32_t>& indices,
    std::array<uint32_t, 5>& offsets,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces)
{
    indices.clear();
    const uint32_t count = saturating_count(surfaces.size());
    indices.reserve(count);
    for (uint32_t surface_index = 0; surface_index < count; ++surface_index) {
        indices.push_back(surface_index);
    }
    sort_prepared_surface_indices(indices, surfaces);
    rebuild_prepared_surface_pass_offsets(offsets, indices, surfaces);
}

void append_area_visible_render_model_handle(
    std::vector<nw::render::ModelInstanceHandle>& out,
    const AreaRenderScene& scene,
    uint32_t record_index)
{
    const auto handles = scene.model_instance_handles();
    if (record_index >= handles.size()) {
        return;
    }

    const auto handle = handles[record_index];
    if (handle.valid()) {
        out.push_back(handle);
    }
}

glm::vec4 matrix_row(const glm::mat4& matrix, glm::length_t row) noexcept
{
    return glm::vec4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
}

struct AreaRenderFrustum {
    std::array<glm::vec4, 6> planes{};
    bool valid = false;
};

AreaRenderFrustum make_area_render_frustum(const glm::mat4& view_projection) noexcept
{
    const glm::vec4 row0 = matrix_row(view_projection, 0);
    const glm::vec4 row1 = matrix_row(view_projection, 1);
    const glm::vec4 row2 = matrix_row(view_projection, 2);
    const glm::vec4 row3 = matrix_row(view_projection, 3);

    AreaRenderFrustum result{};
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
        if (length <= 1.0e-6f) {
            result.valid = false;
            continue;
        }
        plane /= length;
    }
    return result;
}

bool bounds_intersects_frustum(const Bounds& bounds, const AreaRenderFrustum& frustum) noexcept
{
    if (!frustum.valid) {
        return true;
    }

    constexpr float kCullPadding = 0.25f;
    for (const glm::vec4& plane : frustum.planes) {
        const glm::vec3 normal{plane};
        const glm::vec3 positive{
            normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
            normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
            normal.z >= 0.0f ? bounds.max.z : bounds.min.z,
        };
        if (glm::dot(normal, positive) + plane.w < -kCullPadding) {
            return false;
        }
    }
    return true;
}

void expand_bounds(Bounds& target, const Bounds& source, bool& initialized) noexcept
{
    if (!initialized) {
        target = source;
        initialized = true;
        return;
    }
    target.min = glm::min(target.min, source.min);
    target.max = glm::max(target.max, source.max);
}

void count_kind(AreaRenderSceneStats& stats, AreaRenderRecordKind kind) noexcept
{
    switch (kind) {
    case AreaRenderRecordKind::tile:
        ++stats.tile_record_count;
        break;
    case AreaRenderRecordKind::creature:
        ++stats.creature_record_count;
        break;
    case AreaRenderRecordKind::door:
        ++stats.door_record_count;
        break;
    case AreaRenderRecordKind::item:
        ++stats.item_record_count;
        break;
    case AreaRenderRecordKind::placeable:
        ++stats.placeable_record_count;
        break;
    case AreaRenderRecordKind::waypoint:
        ++stats.waypoint_record_count;
        break;
    case AreaRenderRecordKind::unknown:
        ++stats.unknown_record_count;
        break;
    }
}

uint32_t area_chunk_id(const PreviewScene& scene, const AreaRenderSourceInfo& info, const nw::render::Bounds& bounds)
{
    if (scene.area_width <= 0 || scene.area_height <= 0) {
        return kInvalidChunkId;
    }
    if (info.object.type != nw::ObjectType::invalid && !info.static_candidate) {
        return kInvalidChunkId;
    }

    int32_t chunk_x = info.tile_x;
    int32_t chunk_y = info.tile_y;
    if (chunk_x < 0 || chunk_y < 0) {
        const glm::vec3 center = bounds.center();
        chunk_x = static_cast<int32_t>(std::floor(center.x / kAreaRenderTileSize));
        chunk_y = static_cast<int32_t>(std::floor(center.y / kAreaRenderTileSize));
    }

    chunk_x = std::clamp(chunk_x, 0, scene.area_width - 1);
    chunk_y = std::clamp(chunk_y, 0, scene.area_height - 1);
    return static_cast<uint32_t>(chunk_y * scene.area_width + chunk_x);
}

std::optional<AreaObjectRay> normalized_area_object_ray(const AreaObjectRay& ray) noexcept
{
    if (!finite_vec3(ray.origin) || !finite_vec3(ray.direction)) {
        return std::nullopt;
    }

    const float direction_length = glm::length(ray.direction);
    if (!std::isfinite(direction_length) || direction_length <= 1.0e-8f) {
        return std::nullopt;
    }
    return AreaObjectRay{
        .origin = ray.origin,
        .direction = ray.direction / direction_length,
    };
}

uint32_t packed_u8_lane(uint32_t value, uint32_t lane) noexcept
{
    return (value >> (lane * 8u)) & 0xffu;
}

template <typename SkinnedVertex>
glm::vec3 skinned_vertex_position(
    const SkinnedVertex& vertex,
    std::span<const glm::mat4> bones) noexcept
{
    glm::mat4 skin{0.0f};
    for (uint32_t lane = 0; lane < 4u; ++lane) {
        const uint32_t joint = packed_u8_lane(vertex.joint_indices, lane);
        const float weight = static_cast<float>(packed_u8_lane(vertex.joint_weights, lane)) / 255.0f;
        if (joint < bones.size()) {
            skin += bones[joint] * weight;
        }
    }
    return glm::vec3(skin * glm::vec4(vertex.position, 1.0f));
}

template <typename Index, typename PositionAt>
void trace_indexed_triangles(
    const AreaObjectRay& ray,
    const Index* indices,
    uint32_t index_count,
    uint32_t vertex_count,
    PositionAt&& position_at,
    float& nearest_distance) noexcept
{
    if (!indices) {
        return;
    }

    const uint32_t triangle_index_count = index_count - index_count % 3u;
    for (uint32_t offset = 0; offset < triangle_index_count; offset += 3u) {
        const uint32_t i0 = static_cast<uint32_t>(indices[offset]);
        const uint32_t i1 = static_cast<uint32_t>(indices[offset + 1u]);
        const uint32_t i2 = static_cast<uint32_t>(indices[offset + 2u]);
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            continue;
        }

        const AreaSurfaceTriangle triangle{
            .v0 = position_at(i0),
            .v1 = position_at(i1),
            .v2 = position_at(i2),
        };
        const auto distance = ray_triangle_intersection(ray, triangle);
        if (distance && *distance < nearest_distance) {
            nearest_distance = *distance;
        }
    }
}

template <typename Vertex>
void trace_static_geometry(
    const AreaObjectRay& ray,
    const Vertex* vertices,
    uint32_t vertex_count,
    const void* indices,
    uint32_t index_count,
    uint32_t index_stride,
    const glm::mat4& world,
    float& nearest_distance) noexcept
{
    if (!vertices || !indices) {
        return;
    }
    const auto position_at = [vertices, &world](uint32_t index) noexcept {
        return glm::vec3(world * glm::vec4(vertices[index].position, 1.0f));
    };
    if (index_stride == sizeof(uint16_t)) {
        trace_indexed_triangles(
            ray, static_cast<const uint16_t*>(indices), index_count, vertex_count, position_at, nearest_distance);
    } else if (index_stride == sizeof(uint32_t)) {
        trace_indexed_triangles(
            ray, static_cast<const uint32_t*>(indices), index_count, vertex_count, position_at, nearest_distance);
    }
}

template <typename Vertex>
void trace_skinned_geometry(
    const AreaObjectRay& ray,
    const Vertex* vertices,
    uint32_t vertex_count,
    const void* indices,
    uint32_t index_count,
    uint32_t index_stride,
    std::span<const glm::mat4> bones,
    const glm::mat4& world,
    float& nearest_distance) noexcept
{
    if (!vertices || !indices || bones.empty()) {
        return;
    }
    const auto position_at = [vertices, bones, &world](uint32_t index) noexcept {
        const glm::vec3 local = skinned_vertex_position(vertices[index], bones);
        return glm::vec3(world * glm::vec4(local, 1.0f));
    };
    if (index_stride == sizeof(uint16_t)) {
        trace_indexed_triangles(
            ray, static_cast<const uint16_t*>(indices), index_count, vertex_count, position_at, nearest_distance);
    } else if (index_stride == sizeof(uint32_t)) {
        trace_indexed_triangles(
            ray, static_cast<const uint32_t*>(indices), index_count, vertex_count, position_at, nearest_distance);
    }
}

bool build_render_model_selection_bones(
    std::array<glm::mat4, nw::render::kModelMaxSkinBones>& bones,
    const nw::render::RenderModel& model,
    const nw::render::Primitive& primitive,
    const nw::render::ModelInstance* instance) noexcept
{
    std::span<const glm::mat4> source_matrices;
    bool source_available = false;
    if (instance && primitive.skin < instance->animation.skin_matrices.size()) {
        const auto& sampled = instance->animation.skin_matrices[primitive.skin];
        source_matrices = sampled;
        source_available = true;
    }
    uint32_t matrix_count = 0;
    return nw::render::build_model_primitive_skinning_matrices(
        bones,
        model,
        primitive,
        source_matrices,
        source_available,
        matrix_count);
}

void trace_render_model_record(
    const AreaObjectRay& ray,
    uint32_t model_index,
    const nw::render::ModelInstanceHandle instance_handle,
    const PreviewScene& scene,
    float& nearest_distance) noexcept
{
    if (model_index >= scene.static_models.size() || !scene.static_models[model_index]) {
        return;
    }
    const auto* instance = scene.model_instances.get(instance_handle);
    if (!instance || instance->render_model_index != model_index) {
        return;
    }

    const auto& model = *scene.static_models[model_index];
    for (const auto& primitive : model.primitives) {
        if (primitive.vertex_count == 0u || primitive.index_count == 0u
            || !primitive.vertices.valid() || !primitive.indices.valid()) {
            continue;
        }

        const auto* vertices = nw::gfx::map_buffer(primitive.vertices);
        const auto* indices = nw::gfx::map_buffer(primitive.indices);
        if (!vertices || !indices) {
            if (vertices) {
                nw::gfx::unmap_buffer(primitive.vertices);
            }
            if (indices) {
                nw::gfx::unmap_buffer(primitive.indices);
            }
            continue;
        }

        const glm::mat4 world = nw::render::model_instance_primitive_world_transform(
            instance, instance->root_transform, primitive);
        if (primitive.skinned) {
            std::array<glm::mat4, nw::render::kModelMaxSkinBones> bones;
            if (build_render_model_selection_bones(bones, model, primitive, instance)) {
                trace_skinned_geometry(
                    ray,
                    static_cast<const nw::render::SkinnedVertex*>(vertices),
                    primitive.vertex_count,
                    indices,
                    primitive.index_count,
                    primitive.index_stride,
                    bones,
                    world,
                    nearest_distance);
            }
        } else {
            trace_static_geometry(
                ray,
                static_cast<const nw::render::Vertex*>(vertices),
                primitive.vertex_count,
                indices,
                primitive.index_count,
                primitive.index_stride,
                world,
                nearest_distance);
        }
        nw::gfx::unmap_buffer(primitive.vertices);
        nw::gfx::unmap_buffer(primitive.indices);
    }
}

bool valid_area_object_selection_records(const AreaRenderScene& records) noexcept
{
    const size_t count = records.bounds().size();
    return count <= std::numeric_limits<uint32_t>::max()
        && records.flags().size() == count
        && records.kinds().size() == count
        && records.object_handles().size() == count
        && records.model_indices().size() == count
        && records.model_instance_handles().size() == count
        && records.tile_xs().size() == count
        && records.tile_ys().size() == count;
}

bool debug_selection_category_enabled(
    const DebugShapeSelectionRange& range,
    AreaObjectSelectionOptions options) noexcept
{
    switch (range.category) {
    case DebugShapeCategory::trigger:
        return options.triggers_enabled && range.object.type == nw::ObjectType::trigger;
    case DebugShapeCategory::encounter:
        return options.encounters_enabled && range.object.type == nw::ObjectType::encounter;
    case DebugShapeCategory::general:
        return false;
    }
    return false;
}

bool point_on_polygon_edge_xy(
    const glm::vec2& point,
    const glm::vec2& a,
    const glm::vec2& b) noexcept
{
    const glm::vec2 edge = b - a;
    const glm::vec2 offset = point - a;
    const float edge_length_squared = glm::dot(edge, edge);
    if (edge_length_squared <= 1.0e-10f) {
        return glm::dot(offset, offset) <= 1.0e-8f;
    }
    const float cross = edge.x * offset.y - edge.y * offset.x;
    if (std::abs(cross) > 1.0e-4f * std::sqrt(edge_length_squared)) {
        return false;
    }
    const float projection = glm::dot(offset, edge);
    return projection >= 0.0f && projection <= edge_length_squared;
}

bool point_in_polygon_xy(const glm::vec2& point, std::span<const glm::vec3> polygon) noexcept
{
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    size_t previous = polygon.size() - 1u;
    for (size_t current = 0; current < polygon.size(); ++current) {
        const glm::vec2 a{polygon[previous]};
        const glm::vec2 b{polygon[current]};
        if (point_on_polygon_edge_xy(point, a, b)) {
            return true;
        }
        if ((a.y > point.y) != (b.y > point.y)) {
            const float crossing_x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (point.x < crossing_x) {
                inside = !inside;
            }
        }
        previous = current;
    }
    return inside;
}

void trace_debug_shape_selection_range(
    const AreaObjectRay& ray,
    const DebugShapeSelectionRange& range,
    const PreviewScene& scene,
    float& nearest_distance) noexcept
{
    const auto bounds_distance = ray_bounds_intersection(ray, range.bounds);
    if (!bounds_distance || *bounds_distance >= nearest_distance) {
        return;
    }

    if (range.point_count >= 3u) {
        const uint64_t point_end = static_cast<uint64_t>(range.first_point) + range.point_count;
        if (point_end > scene.debug_shape_selection_points.size()
            || !std::isfinite(range.plane_z)
            || std::abs(ray.direction.z) <= 1.0e-8f) {
            return;
        }
        const float distance = (range.plane_z - ray.origin.z) / ray.direction.z;
        if (!std::isfinite(distance) || distance < 0.0f || distance >= nearest_distance) {
            return;
        }
        const glm::vec3 position = ray.origin + ray.direction * distance;
        const std::span polygon{
            scene.debug_shape_selection_points.data() + range.first_point,
            static_cast<size_t>(range.point_count)};
        if (point_in_polygon_xy(glm::vec2{position}, polygon)) {
            nearest_distance = distance;
        }
        return;
    }

    if (range.debug_shape_range_index >= scene.debug_shape_ranges.size()) {
        return;
    }
    const auto& debug_range = scene.debug_shape_ranges[range.debug_shape_range_index];
    const uint64_t index_end = static_cast<uint64_t>(debug_range.first_index) + debug_range.index_count;
    if (index_end > scene.debug_shape_indices.size()
        || scene.debug_shape_vertices.size() > std::numeric_limits<uint32_t>::max()) {
        return;
    }
    const auto position_at = [&scene](uint32_t index) noexcept {
        return scene.debug_shape_vertices[index].position;
    };
    trace_indexed_triangles(
        ray,
        scene.debug_shape_indices.data() + debug_range.first_index,
        debug_range.index_count,
        static_cast<uint32_t>(scene.debug_shape_vertices.size()),
        position_at,
        nearest_distance);
}

AreaObjectSelection select_area_object_geometry(
    const AreaObjectRay& ray,
    const AreaRenderScene& records,
    const PreviewScene& scene,
    AreaObjectSelectionOptions options)
{
    if (options.target != AreaObjectSelectionTarget::object
        && options.target != AreaObjectSelectionTarget::tile) {
        return {};
    }
    const auto normalized_ray = normalized_area_object_ray(ray);
    if (!normalized_ray) {
        return {};
    }

    const auto bounds = records.bounds();
    const auto flags = records.flags();
    const auto kinds = records.kinds();
    const auto objects = records.object_handles();
    const auto model_indices = records.model_indices();
    const auto instance_handles = records.model_instance_handles();
    const auto tile_xs = records.tile_xs();
    const auto tile_ys = records.tile_ys();
    AreaObjectSelection result;
    float nearest_distance = std::numeric_limits<float>::infinity();
    const bool select_tiles = options.target == AreaObjectSelectionTarget::tile;

    for (uint32_t record_index = 0; record_index < bounds.size(); ++record_index) {
        if ((flags[record_index] & AreaRenderScene::RecordFlag::render_enabled) == 0u) {
            continue;
        }
        const bool tile_record = kinds[record_index] == AreaRenderRecordKind::tile
            && tile_xs[record_index] >= 0
            && tile_ys[record_index] >= 0;
        const bool object_record = object_matches_record_kind(kinds[record_index], objects[record_index])
            && nw::kernel::objects().valid(objects[record_index]);
        if ((select_tiles && !tile_record) || (!select_tiles && !object_record)) {
            continue;
        }
        const auto bounds_distance = ray_bounds_intersection(*normalized_ray, bounds[record_index]);
        if (!bounds_distance || *bounds_distance >= nearest_distance) {
            continue;
        }

        float record_distance = nearest_distance;
        trace_render_model_record(
            *normalized_ray,
            model_indices[record_index],
            instance_handles[record_index],
            scene,
            record_distance);
        if (record_distance >= nearest_distance) {
            continue;
        }

        nearest_distance = record_distance;
        result = {
            .record_index = record_index,
            .object = objects[record_index],
            .position = normalized_ray->origin + normalized_ray->direction * record_distance,
            .distance = record_distance,
            .tile_x = tile_xs[record_index],
            .tile_y = tile_ys[record_index],
            .kind = kinds[record_index],
            .source = AreaObjectSelectionSource::area_record,
            .status = AreaObjectSelectionStatus::hit,
        };
    }

    if (!select_tiles) {
        const size_t debug_range_count = std::min<size_t>(
            scene.debug_shape_selection_ranges.size(),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
        for (size_t range_index = 0; range_index < debug_range_count; ++range_index) {
            const auto& range = scene.debug_shape_selection_ranges[range_index];
            if (!debug_selection_category_enabled(range, options)
                || !nw::kernel::objects().valid(range.object)
                || !finite_ordered_bounds(range.bounds)) {
                continue;
            }

            float range_distance = nearest_distance;
            trace_debug_shape_selection_range(*normalized_ray, range, scene, range_distance);
            if (range_distance >= nearest_distance) {
                continue;
            }

            nearest_distance = range_distance;
            result = {
                .record_index = static_cast<uint32_t>(range_index),
                .object = range.object,
                .position = normalized_ray->origin + normalized_ray->direction * range_distance,
                .distance = range_distance,
                .source = AreaObjectSelectionSource::debug_shape,
                .status = AreaObjectSelectionStatus::hit,
            };
        }
    }

    if (result.status == AreaObjectSelectionStatus::hit) {
        return result;
    }
    AreaObjectSelection miss;
    miss.status = AreaObjectSelectionStatus::miss;
    return miss;
}

} // namespace

void trace_area_surfaces(
    std::span<const AreaObjectRay> rays,
    std::span<const AreaSurfaceRange> ranges,
    std::span<const AreaSurfaceTriangle> triangles,
    std::span<AreaSurfaceHit> hits) noexcept
{
    std::fill(hits.begin(), hits.end(), AreaSurfaceHit{});
    if (rays.size() != hits.size()
        || !valid_surface_protocol(ranges, triangles)) {
        return;
    }

    for (size_t ray_index = 0; ray_index < rays.size(); ++ray_index) {
        const auto& input_ray = rays[ray_index];
        auto& result = hits[ray_index];
        if (!finite_vec3(input_ray.origin) || !finite_vec3(input_ray.direction)) {
            continue;
        }

        const float direction_length = glm::length(input_ray.direction);
        if (!std::isfinite(direction_length) || direction_length <= 1.0e-8f) {
            continue;
        }
        const AreaObjectRay ray{
            .origin = input_ray.origin,
            .direction = input_ray.direction / direction_length,
        };

        result.status = AreaSurfaceHitStatus::miss;
        float nearest_distance = std::numeric_limits<float>::infinity();
        for (uint32_t range_index = 0; range_index < ranges.size(); ++range_index) {
            const auto& range = ranges[range_index];
            const uint64_t triangle_end = static_cast<uint64_t>(range.first_triangle)
                + static_cast<uint64_t>(range.triangle_count);
            const auto bounds_distance = ray_bounds_intersection(ray, range.bounds);
            if (!bounds_distance || *bounds_distance > nearest_distance) {
                continue;
            }
            for (uint32_t triangle_index = range.first_triangle;
                triangle_index < triangle_end;
                ++triangle_index) {
                const auto distance = ray_triangle_intersection(ray, triangles[triangle_index]);
                if (!distance || *distance >= nearest_distance) {
                    continue;
                }
                glm::vec3 normal{0.0f};
                if (!upward_surface_normal(triangles[triangle_index], normal)) {
                    continue;
                }

                nearest_distance = *distance;
                result = {
                    .position = ray.origin + ray.direction * nearest_distance,
                    .normal = normal,
                    .distance = nearest_distance,
                    .range_index = range_index,
                    .status = AreaSurfaceHitStatus::hit,
                };
            }
        }
    }
}

AreaSurfaceHit trace_area_surface(
    const AreaObjectRay& ray,
    std::span<const AreaSurfaceRange> ranges,
    std::span<const AreaSurfaceTriangle> triangles) noexcept
{
    AreaSurfaceHit result;
    trace_area_surfaces(
        std::span<const AreaObjectRay>{&ray, 1u}, ranges, triangles,
        std::span<AreaSurfaceHit>{&result, 1u});
    return result;
}

void select_area_objects(
    std::span<const AreaObjectRay> rays,
    const AreaRenderScene& records,
    const PreviewScene& scene,
    std::span<AreaObjectSelection> selections,
    AreaObjectSelectionOptions options)
{
    std::fill(selections.begin(), selections.end(), AreaObjectSelection{});
    if (rays.size() != selections.size() || !valid_area_object_selection_records(records)) {
        return;
    }

    for (size_t i = 0; i < rays.size(); ++i) {
        selections[i] = select_area_object_geometry(
            rays[i], records, scene, options);
    }
}

AreaObjectSelection select_area_object(
    const AreaObjectRay& ray,
    const AreaRenderScene& records,
    const PreviewScene& scene,
    AreaObjectSelectionOptions options)
{
    AreaObjectSelection result;
    select_area_objects(
        std::span<const AreaObjectRay>{&ray, 1u},
        records,
        scene,
        std::span<AreaObjectSelection>{&result, 1u},
        options);
    return result;
}

std::optional<nw::render::Bounds> area_tile_selection_bounds(
    const AreaObjectSelection& selection,
    const AreaRenderScene& records) noexcept
{
    if (selection.status != AreaObjectSelectionStatus::hit
        || selection.source != AreaObjectSelectionSource::area_record
        || selection.kind != AreaRenderRecordKind::tile
        || selection.object.type != nw::ObjectType::invalid
        || selection.tile_x < 0
        || selection.tile_y < 0
        || !valid_area_object_selection_records(records)) {
        return std::nullopt;
    }

    const size_t record_index = selection.record_index;
    if (record_index >= records.bounds().size()
        || records.kinds()[record_index] != AreaRenderRecordKind::tile
        || records.tile_xs()[record_index] != selection.tile_x
        || records.tile_ys()[record_index] != selection.tile_y
        || (records.flags()[record_index] & AreaRenderScene::RecordFlag::render_enabled) == 0u) {
        return std::nullopt;
    }

    const float elevation = records.root_transforms()[record_index][3].z;
    if (!std::isfinite(elevation)) {
        return std::nullopt;
    }

    const float tile_min_x = static_cast<float>(selection.tile_x) * kAreaRenderTileSize;
    const float tile_min_y = static_cast<float>(selection.tile_y) * kAreaRenderTileSize;
    return nw::render::Bounds{
        .min = {tile_min_x, tile_min_y, elevation},
        .max = {
            tile_min_x + kAreaRenderTileSize,
            tile_min_y + kAreaRenderTileSize,
            elevation + kAreaTileSelectionOutlineHeight,
        },
    };
}

AreaObjectBounds collect_area_object_bounds(
    nw::ObjectHandle object,
    std::span<const nw::render::Bounds> bounds,
    std::span<const uint8_t> flags,
    std::span<const AreaRenderRecordKind> kinds,
    std::span<const nw::ObjectHandle> objects) noexcept
{
    AreaObjectBounds result;
    if (object.type == nw::ObjectType::invalid
        || bounds.size() != flags.size()
        || bounds.size() != kinds.size()
        || bounds.size() != objects.size()
        || bounds.size() > std::numeric_limits<uint32_t>::max()) {
        return result;
    }

    for (uint32_t record_index = 0; record_index < bounds.size(); ++record_index) {
        const auto& current = bounds[record_index];
        if (objects[record_index] != object
            || (flags[record_index] & AreaRenderScene::RecordFlag::render_enabled) == 0u
            || !object_matches_record_kind(kinds[record_index], objects[record_index])
            || !finite_ordered_bounds(current)) {
            continue;
        }

        if (result.record_count == 0) {
            result.bounds = current;
        } else {
            result.bounds.min = glm::min(result.bounds.min, current.min);
            result.bounds.max = glm::max(result.bounds.max, current.max);
        }
        ++result.record_count;
    }

    result.status = result.record_count == 0
        ? AreaObjectBoundsStatus::not_found
        : AreaObjectBoundsStatus::found;
    return result;
}

bool should_use_sorted_area_static_surface_lists(
    uint32_t visible_prepared_surface_count,
    uint32_t total_prepared_surface_count) noexcept
{
    if (visible_prepared_surface_count < kSortedVisibleStaticSurfaceThreshold || total_prepared_surface_count == 0u) {
        return false;
    }
    if (visible_prepared_surface_count >= total_prepared_surface_count) {
        return true;
    }
    return static_cast<uint64_t>(visible_prepared_surface_count) * 100u
        >= static_cast<uint64_t>(total_prepared_surface_count)
        * kSortedVisibleStaticSurfaceMinimumSceneCoveragePercent;
}

void AreaRenderScene::clear()
{
    model_indices_.clear();
    render_model_record_indices_.clear();
    model_instance_handles_.clear();
    bounds_.clear();
    root_transforms_.clear();
    pass_masks_.clear();
    flags_.clear();
    chunk_ids_.clear();
    kinds_.clear();
    object_handles_.clear();
    tile_xs_.clear();
    tile_ys_.clear();
    chunk_bounds_.clear();
    chunk_has_bounds_.clear();
    chunk_offsets_.clear();
    chunk_record_indices_.clear();
    surface_ranges_.clear();
    surface_triangles_.clear();
    prepared_model_draws_.clear();
    prepared_model_draw_ranges_.clear();
    prepared_model_surface_draws_.clear();
    prepared_surface_offsets_.clear();
    prepared_surface_indices_.clear();
    prepared_surface_pass_offsets_.fill(0u);
    light_index_offsets_.clear();
    light_indices_.clear();
    dynamic_light_indices_.clear();
    chunk_light_index_offsets_.clear();
    chunk_light_indices_.clear();
    scene_bounds_ = {};
    stats_ = {};
    has_scene_bounds_ = false;
}

void AreaRenderScene::rebuild(const PreviewScene& scene)
{
    clear();
    stats_.chunk_width = scene.area_width > 0 ? static_cast<uint32_t>(scene.area_width) : 0u;
    stats_.chunk_height = scene.area_height > 0 ? static_cast<uint32_t>(scene.area_height) : 0u;
    stats_.chunk_count = stats_.chunk_width * stats_.chunk_height;
    stats_.local_light_count = saturating_count(scene.render_local_lights.size());

    const size_t record_capacity = scene.static_models.size();
    model_indices_.reserve(record_capacity);
    render_model_record_indices_.assign(scene.static_models.size(), kInvalidAreaRenderRecordIndex);
    model_instance_handles_.reserve(record_capacity);
    bounds_.reserve(record_capacity);
    root_transforms_.reserve(record_capacity);
    pass_masks_.reserve(record_capacity);
    flags_.reserve(record_capacity);
    chunk_ids_.reserve(record_capacity);
    kinds_.reserve(record_capacity);
    object_handles_.reserve(record_capacity);
    tile_xs_.reserve(record_capacity);
    tile_ys_.reserve(record_capacity);
    prepared_model_draws_.instance_offsets.reserve(record_capacity + 1u);
    prepared_model_draws_.instance_offsets.push_back(0);
    std::vector<uint32_t> chunk_counts(stats_.chunk_count, 0u);
    chunk_bounds_.resize(stats_.chunk_count);
    chunk_has_bounds_.resize(stats_.chunk_count, 0u);
    uint32_t chunked_record_count = 0;
    bool surface_cache_valid = true;

    for (size_t i = 0; i < scene.static_models.size(); ++i) {
        const auto& model_ptr = scene.static_models[i];
        if (!model_ptr) {
            continue;
        }

        const auto& model = *model_ptr;
        const nw::render::ModelInstanceHandle instance_handle = i < scene.static_model_instance_handles.size()
            ? scene.static_model_instance_handles[i]
            : nw::render::ModelInstanceHandle{};
        const auto* instance = scene.model_instances.get(instance_handle);
        const bool valid_instance = instance
            && instance->render_model_index == static_cast<uint32_t>(i);
        const AreaRenderSourceInfo& info = source_info_for_render_model(scene, i);
        const nw::render::Bounds current_bounds = valid_instance ? instance->current_bounds : model.bounds;
        expand_bounds(scene_bounds_, current_bounds, has_scene_bounds_);

        const uint8_t pass_mask = render_model_pass_mask(model);
        const bool model_static = info.static_candidate
            && valid_instance
            && !instance->scene_animation_enabled;
        uint8_t flags = 0;
        if (valid_instance && instance->visible) {
            flags |= RecordFlag::render_enabled;
        } else {
            ++stats_.disabled_record_count;
        }
        if (model_static) {
            flags |= RecordFlag::static_candidate;
            ++stats_.static_record_count;
        } else {
            ++stats_.dynamic_record_count;
        }
        const bool record_casts_shadow = valid_instance ? instance->shadow.casts_shadow : render_model_casts_shadow(model);
        if (record_casts_shadow) {
            flags |= RecordFlag::shadow_caster;
            ++stats_.shadow_caster_record_count;
        }
        if (has_opaque_cutout_pass(pass_mask)) {
            ++stats_.opaque_cutout_record_count;
        }
        if (has_water_pass(pass_mask)) {
            ++stats_.water_record_count;
        }
        if (has_transparent_pass(pass_mask)) {
            ++stats_.transparent_record_count;
        }

        count_kind(stats_, info.kind);
        const uint32_t chunk_id = area_chunk_id(scene, info, current_bounds);
        if (chunk_id != kInvalidChunkId && chunk_id < chunk_counts.size()) {
            ++chunk_counts[chunk_id];
            ++chunked_record_count;
            bool chunk_initialized = chunk_has_bounds_[chunk_id] != 0u;
            expand_bounds(chunk_bounds_[chunk_id], current_bounds, chunk_initialized);
            chunk_has_bounds_[chunk_id] = chunk_initialized ? 1u : 0u;
        }

        const uint32_t record_index = saturating_count(model_indices_.size());
        model_indices_.push_back(saturating_count(i));
        if (i < render_model_record_indices_.size()) {
            render_model_record_indices_[i] = record_index;
        }
        model_instance_handles_.push_back(instance_handle);
        bounds_.push_back(current_bounds);
        root_transforms_.push_back(valid_instance ? instance->root_transform : glm::mat4{1.0f});
        pass_masks_.push_back(pass_mask);
        flags_.push_back(flags);
        chunk_ids_.push_back(chunk_id);
        kinds_.push_back(info.kind);
        object_handles_.push_back(info.object);
        if ((flags & RecordFlag::render_enabled) != 0u
            && object_matches_record_kind(info.kind, info.object)
            && nw::kernel::objects().valid(info.object)) {
            ++stats_.selectable_object_record_count;
        }
        tile_xs_.push_back(info.tile_x);
        tile_ys_.push_back(info.tile_y);

        if (surface_cache_valid
            && info.kind == AreaRenderRecordKind::tile
            && (flags & RecordFlag::render_enabled) != 0u) {
            surface_cache_valid = append_tile_surface_range(
                model,
                instance,
                root_transforms_.back(),
                surface_ranges_,
                surface_triangles_);
        }

        ++prepared_model_draws_.stats.handle_count;
        if (!valid_instance) {
            ++prepared_model_draws_.stats.stale_handle_count;
        } else if (!instance->visible) {
            ++prepared_model_draws_.stats.hidden_instance_count;
        } else {
            ++prepared_model_draws_.stats.visible_instance_count;
            if (model_static) {
                const size_t draw_begin = prepared_model_draws_.draws.size();
                append_prepared_render_model_draws(
                    prepared_model_draws_,
                    instance_handle,
                    *instance,
                    model,
                    scene.material_overrides);
                stats_.max_prepared_draws_per_record = std::max(
                    stats_.max_prepared_draws_per_record,
                    saturating_count(prepared_model_draws_.draws.size() - draw_begin));
            }
        }
        prepared_model_draws_.instance_offsets.push_back(saturating_count(prepared_model_draws_.draws.size()));
    }

    stats_.record_count = saturating_count(model_indices_.size());
    stats_.object_handle_bytes = saturating_count(object_handles_.size() * sizeof(nw::ObjectHandle));
    stats_.prepared_draw_count = saturating_count(prepared_model_draws_.draws.size());
    if (!surface_cache_valid) {
        surface_ranges_.clear();
        surface_triangles_.clear();
    }
    stats_.surface_range_count = saturating_count(surface_ranges_.size());
    stats_.surface_triangle_count = saturating_count(surface_triangles_.size());
    stats_.surface_bytes = saturating_count(
        surface_ranges_.size() * sizeof(AreaSurfaceRange)
        + surface_triangles_.size() * sizeof(AreaSurfaceTriangle));
    chunk_offsets_.resize(static_cast<size_t>(stats_.chunk_count) + 1u, 0u);
    for (uint32_t chunk_id = 0; chunk_id < stats_.chunk_count; ++chunk_id) {
        const uint32_t count = chunk_counts[chunk_id];
        if (count > 0) {
            ++stats_.nonempty_chunk_count;
            stats_.max_records_per_chunk = std::max(stats_.max_records_per_chunk, count);
        }
        chunk_offsets_[static_cast<size_t>(chunk_id) + 1u] = chunk_offsets_[chunk_id] + count;
    }

    chunk_record_indices_.resize(chunked_record_count);
    std::vector<uint32_t> write_offsets = chunk_offsets_;
    for (uint32_t record_index = 0; record_index < chunk_ids_.size(); ++record_index) {
        const uint32_t chunk_id = chunk_ids_[record_index];
        if (chunk_id == kInvalidChunkId || chunk_id >= stats_.chunk_count) {
            continue;
        }
        const uint32_t write_index = write_offsets[chunk_id]++;
        chunk_record_indices_[write_index] = record_index;
    }

    refresh_light_indices(scene);
    nw::render::collect_prepared_model_draw_ranges(prepared_model_draw_ranges_, prepared_model_draws_);
    nw::render::collect_prepared_model_surface_draws(
        prepared_model_surface_draws_,
        prepared_model_draws_,
        prepared_model_draw_ranges_);
    prepared_surface_offsets_.assign(model_indices_.size() + 1u, 0u);
    size_t surface_cursor = 0;
    for (uint32_t record_index = 0; record_index < model_indices_.size(); ++record_index) {
        prepared_surface_offsets_[record_index] = saturating_count(surface_cursor);
        while (surface_cursor < prepared_model_surface_draws_.draws.size()
            && prepared_model_surface_draws_.draws[surface_cursor].handle_index == record_index) {
            ++surface_cursor;
        }
    }
    if (!prepared_surface_offsets_.empty()) {
        prepared_surface_offsets_.back() = saturating_count(surface_cursor);
    }
    const std::span<const nw::render::PreparedModelSurfaceDraw> prepared_surfaces{
        prepared_model_surface_draws_.draws.data(),
        prepared_model_surface_draws_.draws.size()};
    rebuild_prepared_surface_indices(prepared_surface_indices_, prepared_surface_pass_offsets_, prepared_surfaces);
}

void AreaRenderScene::refresh_runtime_records(const PreviewScene& scene)
{
    const size_t record_count = std::min({
        model_indices_.size(),
        model_instance_handles_.size(),
        flags_.size(),
        bounds_.size(),
        root_transforms_.size(),
    });

    for (size_t record_index = 0; record_index < record_count; ++record_index) {
        const uint32_t model_index = model_indices_[record_index];
        const auto* instance = scene.model_instances.get(model_instance_handles_[record_index]);
        const bool valid_instance = instance
            && instance->render_model_index == model_index;
        if (!valid_instance) {
            set_flag(flags_[record_index], RecordFlag::render_enabled, false);
            set_flag(flags_[record_index], RecordFlag::shadow_caster, false);
            continue;
        }

        set_flag(flags_[record_index], RecordFlag::render_enabled, instance->visible);
        if (has_flag(flags_[record_index], RecordFlag::static_candidate)) {
            continue;
        }

        bounds_[record_index] = instance->current_bounds;
        root_transforms_[record_index] = instance->root_transform;
        set_flag(flags_[record_index], RecordFlag::shadow_caster, instance->shadow.casts_shadow);
    }
}

void AreaRenderScene::refresh_light_indices(const PreviewScene& scene)
{
    light_index_offsets_.clear();
    light_indices_.clear();
    dynamic_light_indices_.clear();
    chunk_light_index_offsets_.clear();
    chunk_light_indices_.clear();
    stats_.local_light_count = saturating_count(scene.render_local_lights.size());
    stats_.light_index_count = 0;
    stats_.dynamic_light_count = 0;
    stats_.max_light_indices_per_record = 0;
    stats_.chunk_light_index_count = 0;
    stats_.max_light_indices_per_chunk = 0;

    const size_t record_count = model_indices_.size();
    dynamic_light_indices_.reserve(scene.local_lights.size());
    for (size_t light_index = 0; light_index < scene.local_lights.size(); ++light_index) {
        const auto& light = scene.local_lights[light_index];
        if (light.model_index < scene.static_models.size()) {
            dynamic_light_indices_.push_back(saturating_count(light_index));
        }
    }
    stats_.dynamic_light_count = saturating_count(dynamic_light_indices_.size());

    light_index_offsets_.reserve(record_count + 1u);
    light_index_offsets_.push_back(0);
    for (size_t record_index = 0; record_index < record_count; ++record_index) {
        const size_t light_index_begin = light_indices_.size();
        if (record_index < flags_.size() && record_index < bounds_.size()
            && has_flag(flags_[record_index], AreaRenderScene::RecordFlag::static_candidate)
            && !scene.render_local_lights.empty()) {
            collect_local_light_indices_for_bounds(
                light_indices_, scene.render_local_lights, bounds_[record_index]);
        }

        const size_t light_index_end = light_indices_.size();
        const uint32_t light_index_count = saturating_count(light_index_end - light_index_begin);
        stats_.max_light_indices_per_record = std::max(
            stats_.max_light_indices_per_record,
            light_index_count);
        light_index_offsets_.push_back(saturating_count(light_indices_.size()));
    }
    stats_.light_index_count = saturating_count(light_indices_.size());

    chunk_light_index_offsets_.resize(static_cast<size_t>(stats_.chunk_count) + 1u, 0u);
    if (stats_.chunk_count > 0 && stats_.local_light_count > 0) {
        std::vector<uint32_t> light_marks(stats_.local_light_count, 0u);
        uint32_t light_generation = 1;
        chunk_light_indices_.reserve(light_indices_.size());
        for (uint32_t chunk_id = 0; chunk_id < stats_.chunk_count; ++chunk_id) {
            if (light_generation == std::numeric_limits<uint32_t>::max()) {
                std::fill(light_marks.begin(), light_marks.end(), 0u);
                light_generation = 1;
            }
            ++light_generation;

            const uint32_t begin = chunk_offsets_[chunk_id];
            const uint32_t end = chunk_offsets_[static_cast<size_t>(chunk_id) + 1u];
            const uint32_t clamped_end = std::min<uint32_t>(end, saturating_count(chunk_record_indices_.size()));
            const size_t chunk_light_begin = chunk_light_indices_.size();
            for (uint32_t offset = begin; offset < clamped_end; ++offset) {
                for (const uint32_t light_index : light_indices_for_record(chunk_record_indices_[offset])) {
                    if (light_index >= light_marks.size() || light_marks[light_index] == light_generation) {
                        continue;
                    }
                    light_marks[light_index] = light_generation;
                    chunk_light_indices_.push_back(light_index);
                }
            }
            stats_.max_light_indices_per_chunk = std::max(
                stats_.max_light_indices_per_chunk,
                saturating_count(chunk_light_indices_.size() - chunk_light_begin));
            chunk_light_index_offsets_[static_cast<size_t>(chunk_id) + 1u] = saturating_count(chunk_light_indices_.size());
        }
    }
    stats_.chunk_light_index_count = saturating_count(chunk_light_indices_.size());
}

std::span<const nw::render::PreparedModelDraw> AreaRenderScene::prepared_model_draws_for_record(
    uint32_t record_index) const noexcept
{
    return nw::render::prepared_model_draws_for_handle_index(prepared_model_draws_, record_index);
}

std::span<const nw::render::PreparedModelSurfaceDraw> AreaRenderScene::prepared_model_surface_draws_for_record(
    uint32_t record_index) const noexcept
{
    if (static_cast<size_t>(record_index) + 1u >= prepared_surface_offsets_.size()) {
        return {};
    }
    const uint32_t begin = prepared_surface_offsets_[record_index];
    const uint32_t end = prepared_surface_offsets_[static_cast<size_t>(record_index) + 1u];
    if (end <= begin || end > prepared_model_surface_draws_.draws.size()) {
        return {};
    }
    return std::span<const nw::render::PreparedModelSurfaceDraw>{
        prepared_model_surface_draws_.draws.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

std::span<const uint32_t> AreaRenderScene::opaque_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 0u, 1u);
}

std::span<const uint32_t> AreaRenderScene::cutout_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 1u, 2u);
}

std::span<const uint32_t> AreaRenderScene::opaque_cutout_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 0u, 2u);
}

std::span<const uint32_t> AreaRenderScene::water_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 2u, 3u);
}

std::span<const uint32_t> AreaRenderScene::transparent_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 3u, 4u);
}

std::span<const uint32_t> AreaRenderScene::light_indices_for_record(uint32_t record_index) const noexcept
{
    if (static_cast<size_t>(record_index) + 1u >= light_index_offsets_.size()) {
        return {};
    }
    const uint32_t begin = light_index_offsets_[record_index];
    const uint32_t end = light_index_offsets_[static_cast<size_t>(record_index) + 1u];
    if (end <= begin || end > light_indices_.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        light_indices_.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

std::span<const uint32_t> AreaRenderScene::light_indices_for_chunk(uint32_t chunk_id) const noexcept
{
    if (static_cast<size_t>(chunk_id) + 1u >= chunk_light_index_offsets_.size()) {
        return {};
    }
    const uint32_t begin = chunk_light_index_offsets_[chunk_id];
    const uint32_t end = chunk_light_index_offsets_[static_cast<size_t>(chunk_id) + 1u];
    if (end <= begin || end > chunk_light_indices_.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        chunk_light_indices_.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

size_t rebuild_area_visibility_mask(
    const AreaRenderScene& scene,
    const AreaVisibilityMaskOptions& options,
    std::vector<uint8_t>& mask)
{
    const auto& stats = scene.stats();
    const uint32_t width = stats.chunk_width;
    const uint32_t height = stats.chunk_height;
    const uint32_t chunk_count = stats.chunk_count;
    if (options.radius_tiles < 0 || width == 0 || height == 0 || chunk_count != width * height) {
        mask.clear();
        return 0;
    }

    mask.resize(chunk_count);
    std::fill(mask.begin(), mask.end(), uint8_t{0u});

    const int32_t center_x = std::clamp(
        static_cast<int32_t>(std::floor(options.camera_position.x / kAreaRenderTileSize)),
        0,
        static_cast<int32_t>(width) - 1);
    const int32_t center_y = std::clamp(
        static_cast<int32_t>(std::floor(options.camera_position.y / kAreaRenderTileSize)),
        0,
        static_cast<int32_t>(height) - 1);
    const int32_t radius = std::max(options.radius_tiles, 0);
    const int32_t min_x = std::max(center_x - radius, 0);
    const int32_t max_x = std::min(center_x + radius, static_cast<int32_t>(width) - 1);
    const int32_t min_y = std::max(center_y - radius, 0);
    const int32_t max_y = std::min(center_y + radius, static_cast<int32_t>(height) - 1);

    constexpr float kPi = 3.14159265358979323846f;
    const glm::vec2 camera_xy{options.camera_position.x, options.camera_position.y};
    glm::vec2 forward{
        options.camera_target.x - options.camera_position.x,
        options.camera_target.y - options.camera_position.y,
    };
    const float forward_length_squared = glm::dot(forward, forward);
    const bool use_view_cone = options.mode == AreaVisibilityMaskMode::view_cone
        && forward_length_squared > 1.0e-6f;
    if (use_view_cone) {
        forward /= std::sqrt(forward_length_squared);
    }
    const float half_angle = std::clamp(options.half_angle_degrees, 1.0f, 179.0f);
    const float cone_cos = std::cos(half_angle * kPi / 180.0f);
    const float near_keep_distance = kAreaRenderTileSize * 1.5f;
    const float near_keep_distance_squared = near_keep_distance * near_keep_distance;

    size_t visible_count = 0;
    for (int32_t y = min_y; y <= max_y; ++y) {
        for (int32_t x = min_x; x <= max_x; ++x) {
            bool visible = true;
            if (use_view_cone) {
                const glm::vec2 chunk_center{
                    (static_cast<float>(x) + 0.5f) * kAreaRenderTileSize,
                    (static_cast<float>(y) + 0.5f) * kAreaRenderTileSize,
                };
                const glm::vec2 delta = chunk_center - camera_xy;
                const float distance_squared = glm::dot(delta, delta);
                if (distance_squared > near_keep_distance_squared) {
                    visible = glm::dot(delta, forward) >= std::sqrt(distance_squared) * cone_cos;
                }
            }
            if (visible) {
                mask[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = 1u;
                ++visible_count;
            }
        }
    }
    return visible_count;
}

void AreaRenderFrame::clear()
{
    visible_record_indices_.clear();
    visible_chunk_indices_.clear();
    opaque_cutout_record_indices_.clear();
    water_record_indices_.clear();
    transparent_record_indices_.clear();
    shadow_caster_record_indices_.clear();
    visible_render_model_instance_handles_.clear();
    visible_light_indices_.clear();
    visible_prepared_surface_indices_.clear();
    visible_prepared_surface_pass_offsets_.fill(0u);
    record_marks_.clear();
    chunk_marks_.clear();
    light_marks_.clear();
    visible_bounds_ = {};
    shadow_caster_bounds_ = {};
    cached_draw_scene_ = nullptr;
    stats_ = {};
    record_mark_generation_ = 1;
    mark_generation_ = 1;
    light_mark_generation_ = 1;
    has_visible_bounds_ = false;
    has_shadow_caster_bounds_ = false;
    filtered_light_indices_valid_ = false;
}

void AreaRenderFrame::reserve_for_scene(const AreaRenderScene& scene)
{
    const size_t record_count = scene.model_indices().size();
    visible_record_indices_.reserve(record_count);
    visible_chunk_indices_.reserve(scene.stats().chunk_count);
    opaque_cutout_record_indices_.reserve(record_count);
    water_record_indices_.reserve(record_count);
    transparent_record_indices_.reserve(record_count);
    shadow_caster_record_indices_.reserve(record_count);
    visible_render_model_instance_handles_.reserve(record_count);
    visible_light_indices_.reserve(scene.stats().local_light_count);
    const size_t prepared_surface_count = scene.prepared_model_surface_draws().draws.size();
    visible_prepared_surface_indices_.reserve(prepared_surface_count);
    record_marks_.resize(record_count, 0u);
    chunk_marks_.resize(scene.stats().chunk_count, 0u);
    light_marks_.resize(scene.stats().local_light_count, 0u);
}

std::span<const uint32_t> AreaRenderFrame::visible_opaque_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->opaque_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        0u,
        1u);
}

std::span<const uint32_t> AreaRenderFrame::visible_cutout_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->cutout_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        1u,
        2u);
}

std::span<const uint32_t> AreaRenderFrame::visible_opaque_cutout_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->opaque_cutout_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        0u,
        2u);
}

std::span<const uint32_t> AreaRenderFrame::visible_water_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->water_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        2u,
        3u);
}

std::span<const uint32_t> AreaRenderFrame::visible_transparent_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->transparent_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        3u,
        4u);
}

void prepare_area_frame(const AreaRenderScene& scene, AreaRenderFrame& frame, const AreaRenderCullContext& cull)
{
    frame.reserve_for_scene(scene);
    frame.visible_record_indices_.clear();
    frame.visible_chunk_indices_.clear();
    frame.opaque_cutout_record_indices_.clear();
    frame.water_record_indices_.clear();
    frame.transparent_record_indices_.clear();
    frame.shadow_caster_record_indices_.clear();
    frame.visible_render_model_instance_handles_.clear();
    frame.visible_light_indices_.clear();
    frame.visible_prepared_surface_indices_.clear();
    frame.visible_prepared_surface_pass_offsets_.fill(0u);
    frame.cached_draw_scene_ = nullptr;
    frame.visible_bounds_ = {};
    frame.shadow_caster_bounds_ = {};
    frame.has_visible_bounds_ = false;
    frame.has_shadow_caster_bounds_ = false;
    frame.filtered_light_indices_valid_ = false;
    frame.stats_ = {};

    if (frame.record_mark_generation_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(frame.record_marks_.begin(), frame.record_marks_.end(), 0u);
        frame.record_mark_generation_ = 1;
    }
    ++frame.record_mark_generation_;
    if (frame.mark_generation_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(frame.chunk_marks_.begin(), frame.chunk_marks_.end(), 0u);
        frame.mark_generation_ = 1;
    }
    ++frame.mark_generation_;
    if (frame.light_mark_generation_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(frame.light_marks_.begin(), frame.light_marks_.end(), 0u);
        frame.light_mark_generation_ = 1;
    }
    ++frame.light_mark_generation_;

    const auto record_indices = scene.model_indices();
    const auto pass_masks = scene.pass_masks();
    const auto flags = scene.flags();
    const auto chunk_ids = scene.chunk_ids();
    const auto bounds = scene.bounds();
    const std::span<const nw::render::PreparedModelSurfaceDraw> prepared_surfaces{
        scene.prepared_model_surface_draws().draws.data(),
        scene.prepared_model_surface_draws().draws.size()};
    const auto chunk_bounds = scene.chunk_bounds();
    const auto chunk_has_bounds = scene.chunk_has_bounds();
    const uint32_t chunk_count = scene.stats().chunk_count;
    const bool chunk_visibility_enabled = cull.chunk_visibility_enabled
        && cull.visible_chunk_mask.size() >= chunk_count;
    const bool cull_enabled = cull.enabled
        && chunk_count > 0
        && chunk_bounds.size() >= chunk_count
        && chunk_has_bounds.size() >= chunk_count
        && scene.chunk_offsets().size() > chunk_count;
    const AreaRenderFrustum frustum = cull_enabled ? make_area_render_frustum(cull.view_projection) : AreaRenderFrustum{};

    const auto record_bounds = [&](uint32_t record_index) -> Bounds {
        if (record_index < bounds.size()) {
            return bounds[record_index];
        }
        return {};
    };

    const auto append_visible_record = [&](uint32_t record_index) {
        if (record_index >= record_indices.size() || record_index >= flags.size() || record_index >= chunk_ids.size()) {
            return;
        }
        if (!has_flag(flags[record_index], AreaRenderScene::RecordFlag::render_enabled)) {
            return;
        }

        const Bounds current_bounds = record_bounds(record_index);
        frame.visible_record_indices_.push_back(record_index);
        if (record_index < frame.record_marks_.size()) {
            frame.record_marks_[record_index] = frame.record_mark_generation_;
        }
        expand_bounds(frame.visible_bounds_, current_bounds, frame.has_visible_bounds_);
        if (has_flag(flags[record_index], AreaRenderScene::RecordFlag::static_candidate)) {
            ++frame.stats_.visible_static_record_count;
            const auto surfaces = scene.prepared_model_surface_draws_for_record(record_index);
            frame.stats_.visible_prepared_surface_count += saturating_count(surfaces.size());
        } else {
            ++frame.stats_.visible_dynamic_record_count;
        }

        const uint32_t chunk_id = chunk_ids[record_index];
        if (chunk_id < frame.chunk_marks_.size() && frame.chunk_marks_[chunk_id] != frame.mark_generation_) {
            frame.chunk_marks_[chunk_id] = frame.mark_generation_;
            frame.visible_chunk_indices_.push_back(chunk_id);
            ++frame.stats_.visible_chunk_count;
        }
    };

    const auto append_record_if_visible = [&](uint32_t record_index) {
        if (frustum.valid && record_index < bounds.size()
            && !bounds_intersects_frustum(bounds[record_index], frustum)) {
            ++frame.stats_.culled_record_count;
            return;
        }
        append_visible_record(record_index);
    };

    if (cull_enabled && frustum.valid) {
        const auto chunk_offsets = scene.chunk_offsets();
        const auto chunk_record_indices = scene.chunk_record_indices();
        for (uint32_t chunk_id = 0; chunk_id < chunk_count; ++chunk_id) {
            const uint32_t begin = chunk_offsets[chunk_id];
            const uint32_t end = chunk_offsets[static_cast<size_t>(chunk_id) + 1u];
            if (begin >= end || begin >= chunk_record_indices.size()) {
                continue;
            }

            const uint32_t clamped_end = std::min<uint32_t>(end, saturating_count(chunk_record_indices.size()));
            if (chunk_visibility_enabled && cull.visible_chunk_mask[chunk_id] == 0u) {
                ++frame.stats_.culled_chunk_count;
                ++frame.stats_.visibility_culled_chunk_count;
                const uint32_t culled_records = clamped_end - begin;
                frame.stats_.culled_record_count += culled_records;
                frame.stats_.visibility_culled_record_count += culled_records;
                continue;
            }
            if (chunk_has_bounds[chunk_id] != 0u
                && !bounds_intersects_frustum(chunk_bounds[chunk_id], frustum)) {
                ++frame.stats_.culled_chunk_count;
                frame.stats_.culled_record_count += clamped_end - begin;
                continue;
            }

            for (uint32_t offset = begin; offset < clamped_end; ++offset) {
                append_record_if_visible(chunk_record_indices[offset]);
            }
        }

        for (uint32_t record_index = 0; record_index < chunk_ids.size(); ++record_index) {
            const uint32_t chunk_id = chunk_ids[record_index];
            if (chunk_id == kInvalidChunkId || chunk_id >= chunk_count) {
                append_record_if_visible(record_index);
            }
        }
    } else {
        for (uint32_t record_index = 0; record_index < record_indices.size(); ++record_index) {
            append_visible_record(record_index);
        }
    }

    frame.stats_.visible_record_count = saturating_count(frame.visible_record_indices_.size());
    const bool use_cached_draw_lists = scene.stats().disabled_record_count == 0
        && frame.stats_.visible_static_record_count == scene.stats().static_record_count;
    frame.stats_.uses_cached_draw_lists = use_cached_draw_lists;
    if (use_cached_draw_lists) {
        frame.cached_draw_scene_ = &scene;
    }
    const bool use_sorted_visible_surfaces = !use_cached_draw_lists
        && should_use_sorted_area_static_surface_lists(
            frame.stats_.visible_prepared_surface_count,
            saturating_count(prepared_surfaces.size()));

    const bool build_filtered_lights = scene.stats().local_light_count > 0
        && frame.stats_.visible_record_count < scene.stats().record_count;
    if (build_filtered_lights) {
        frame.filtered_light_indices_valid_ = true;
        const auto append_light_index = [&](uint32_t light_index) {
            if (light_index >= frame.light_marks_.size()
                || frame.light_marks_[light_index] == frame.light_mark_generation_) {
                return;
            }
            frame.light_marks_[light_index] = frame.light_mark_generation_;
            frame.visible_light_indices_.push_back(light_index);
        };

        for (const uint32_t chunk_id : frame.visible_chunk_indices_) {
            for (const uint32_t light_index : scene.light_indices_for_chunk(chunk_id)) {
                append_light_index(light_index);
            }
        }

        for (const uint32_t record_index : frame.visible_record_indices_) {
            if (record_index >= chunk_ids.size()) {
                continue;
            }
            const uint32_t chunk_id = chunk_ids[record_index];
            if (chunk_id != kInvalidChunkId && chunk_id < chunk_count) {
                continue;
            }
            for (const uint32_t light_index : scene.light_indices_for_record(record_index)) {
                append_light_index(light_index);
            }
        }
        for (const uint32_t light_index : scene.dynamic_light_indices()) {
            append_light_index(light_index);
        }
        frame.stats_.visible_light_count = saturating_count(frame.visible_light_indices_.size());
    } else {
        frame.stats_.visible_light_count = scene.stats().local_light_count;
    }

    for (const uint32_t record_index : frame.visible_record_indices_) {
        if (record_index >= pass_masks.size() || record_index >= flags.size()) {
            continue;
        }

        const uint8_t pass_mask = pass_masks[record_index];
        append_area_visible_render_model_handle(
            frame.visible_render_model_instance_handles_,
            scene,
            record_index);
        if (has_opaque_cutout_pass(pass_mask)) {
            frame.opaque_cutout_record_indices_.push_back(record_index);
        }
        if (has_water_pass(pass_mask)) {
            frame.water_record_indices_.push_back(record_index);
        }
        if (has_transparent_pass(pass_mask)) {
            frame.transparent_record_indices_.push_back(record_index);
        }
        if (has_flag(flags[record_index], AreaRenderScene::RecordFlag::shadow_caster)) {
            frame.shadow_caster_record_indices_.push_back(record_index);
            expand_bounds(
                frame.shadow_caster_bounds_,
                record_bounds(record_index),
                frame.has_shadow_caster_bounds_);
        }

        if (use_cached_draw_lists || use_sorted_visible_surfaces
            || !has_flag(flags[record_index], AreaRenderScene::RecordFlag::static_candidate)) {
            continue;
        }
        const auto surfaces = scene.prepared_model_surface_draws_for_record(record_index);
        for (const auto& surface : surfaces) {
            const size_t surface_index = static_cast<size_t>(&surface - prepared_surfaces.data());
            if (surface_index < prepared_surfaces.size()) {
                frame.visible_prepared_surface_indices_.push_back(saturating_count(surface_index));
            }
        }
    }

    if (use_sorted_visible_surfaces) {
        for (const uint32_t surface_index : scene.prepared_surface_indices()) {
            if (surface_index >= prepared_surfaces.size()) {
                continue;
            }
            const uint32_t record_index = prepared_surfaces[surface_index].handle_index;
            if (record_index < frame.record_marks_.size()
                && frame.record_marks_[record_index] == frame.record_mark_generation_) {
                frame.visible_prepared_surface_indices_.push_back(surface_index);
            }
        }
    }
    if (!use_cached_draw_lists) {
        if (!use_sorted_visible_surfaces) {
            sort_prepared_surface_indices(frame.visible_prepared_surface_indices_, prepared_surfaces);
        }
        rebuild_prepared_surface_pass_offsets(
            frame.visible_prepared_surface_pass_offsets_,
            frame.visible_prepared_surface_indices_,
            prepared_surfaces);
    }

    frame.stats_.opaque_cutout_record_count = saturating_count(frame.opaque_cutout_record_indices_.size());
    frame.stats_.water_record_count = saturating_count(frame.water_record_indices_.size());
    frame.stats_.transparent_record_count = saturating_count(frame.transparent_record_indices_.size());
    frame.stats_.shadow_caster_record_count = saturating_count(frame.shadow_caster_record_indices_.size());
}

std::string_view area_render_record_kind_label(AreaRenderRecordKind kind) noexcept
{
    switch (kind) {
    case AreaRenderRecordKind::tile:
        return "tile";
    case AreaRenderRecordKind::creature:
        return "creature";
    case AreaRenderRecordKind::door:
        return "door";
    case AreaRenderRecordKind::item:
        return "item";
    case AreaRenderRecordKind::placeable:
        return "placeable";
    case AreaRenderRecordKind::waypoint:
        return "waypoint";
    case AreaRenderRecordKind::unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace nw::render::viewer
