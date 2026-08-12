#include "scene_debug.hpp"

#include "preview_object.hpp"
#include "preview_scene.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/log.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/render/render_context.hpp>
#include <nw/render/shader_provider.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace nw::render::viewer {

namespace {

struct DebugGridVertex {
    glm::vec3 position{0.0f};
};

nw::Location object_spatial_location(const nw::ObjectBase& object)
{
    return nw::kernel::objects().components().location(object.handle());
}

} // namespace

SceneDebugRenderer::SceneDebugRenderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

SceneDebugRenderer::~SceneDebugRenderer()
{
    if (selection_bounds_indices_.valid()) nw::gfx::destroy_buffer(selection_bounds_indices_);
    if (selection_bounds_vertices_.valid()) nw::gfx::destroy_buffer(selection_bounds_vertices_);
    if (debug_shape_indices_.valid()) nw::gfx::destroy_buffer(debug_shape_indices_);
    if (debug_shape_vertices_.valid()) nw::gfx::destroy_buffer(debug_shape_vertices_);
    if (debug_grid_indices_.valid()) nw::gfx::destroy_buffer(debug_grid_indices_);
    if (debug_grid_vertices_.valid()) nw::gfx::destroy_buffer(debug_grid_vertices_);
    if (debug_shape_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, debug_shape_pipeline_);
    if (debug_grid_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, debug_grid_pipeline_);
}

bool SceneDebugRenderer::initialize(nw::render::ShaderProvider& shader_provider)
{
    if (!ctx_) {
        LOG_F(ERROR, "Scene debug renderer: missing graphics context");
        return false;
    }

    auto vs_debug = shader_provider.get_shader("render_debug_grid.vs.hlsl");
    auto ps_debug = shader_provider.get_shader("render_debug_grid.ps.hlsl");
    if (vs_debug.valid() && ps_debug.valid()) {
        nw::gfx::PipelineDesc debug_desc{};
        debug_desc.vs = vs_debug;
        debug_desc.fs = ps_debug;
        debug_desc.uses_single_texture = false;
        debug_desc.depth_test = true;
        debug_desc.depth_write = false;
        debug_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
        debug_desc.vertex_stride = sizeof(DebugGridVertex);
        debug_desc.vertex_attributes = {
            {0, offsetof(DebugGridVertex, position), nw::gfx::VertexFormat::Float3},
        };
        debug_grid_pipeline_ = nw::gfx::create_pipeline(ctx_, debug_desc);
        if (!debug_grid_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create debug grid pipeline");
        }
    }

    auto vs_debug_shape = shader_provider.get_shader("render_debug_shape.vs.hlsl");
    auto ps_debug_shape = shader_provider.get_shader("render_debug_shape.ps.hlsl");
    if (vs_debug_shape.valid() && ps_debug_shape.valid()) {
        nw::gfx::PipelineDesc debug_shape_desc{};
        debug_shape_desc.vs = vs_debug_shape;
        debug_shape_desc.fs = ps_debug_shape;
        debug_shape_desc.uses_single_texture = false;
        debug_shape_desc.depth_test = true;
        debug_shape_desc.depth_write = false;
        debug_shape_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
        debug_shape_desc.vertex_stride = sizeof(DebugShapeVertex);
        debug_shape_desc.vertex_attributes = {
            {0, offsetof(DebugShapeVertex, position), nw::gfx::VertexFormat::Float3},
            {1, offsetof(DebugShapeVertex, color), nw::gfx::VertexFormat::Float4},
        };
        debug_shape_pipeline_ = nw::gfx::create_pipeline(ctx_, debug_shape_desc);
        if (!debug_shape_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create debug shape pipeline");
        }
    }

    return true;
}

namespace {

struct DebugGridConstants {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec4 minor_color{0.0f};
    glm::vec4 major_color{0.0f};
    glm::vec4 grid_params{1.0f, 10.0f, 0.03f, 0.08f};
};

struct DebugShapeConstants {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

constexpr size_t kSelectionBoundsEdgeCount = 12;
constexpr size_t kSelectionBoundsVertexCount = kSelectionBoundsEdgeCount * 4;
constexpr size_t kSelectionBoundsIndexCount = kSelectionBoundsEdgeCount * 6;

struct SelectionBoundsGeometry {
    std::array<DebugShapeVertex, kSelectionBoundsVertexCount> vertices;
    std::array<uint32_t, kSelectionBoundsIndexCount> indices;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

bool finite_ordered_bounds(const nw::render::Bounds& bounds) noexcept
{
    return std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) && std::isfinite(bounds.min.z)
        && std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z)
        && bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

void append_selection_bounds_edge(
    SelectionBoundsGeometry& out,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& camera_position,
    const glm::vec4& color)
{
    constexpr float kLineWidth = 0.06f;
    const glm::vec3 edge = b - a;
    const float edge_length_squared = glm::dot(edge, edge);
    if (edge_length_squared <= 1.0e-10f
        || out.vertex_count + 4 > out.vertices.size()
        || out.index_count + 6 > out.indices.size()) {
        return;
    }

    glm::vec3 side = glm::cross(edge, camera_position - (a + b) * 0.5f);
    float side_length_squared = glm::dot(side, side);
    if (side_length_squared <= 1.0e-10f) {
        side = glm::cross(edge, glm::vec3{0.0f, 0.0f, 1.0f});
        side_length_squared = glm::dot(side, side);
    }
    if (side_length_squared <= 1.0e-10f) {
        side = glm::cross(edge, glm::vec3{0.0f, 1.0f, 0.0f});
        side_length_squared = glm::dot(side, side);
    }
    if (side_length_squared <= 1.0e-10f) {
        return;
    }
    side *= (0.5f * kLineWidth) / std::sqrt(side_length_squared);

    const uint32_t base = out.vertex_count;
    out.vertices[out.vertex_count++] = {a + side, color};
    out.vertices[out.vertex_count++] = {b + side, color};
    out.vertices[out.vertex_count++] = {b - side, color};
    out.vertices[out.vertex_count++] = {a - side, color};
    out.indices[out.index_count++] = base;
    out.indices[out.index_count++] = base + 1;
    out.indices[out.index_count++] = base + 2;
    out.indices[out.index_count++] = base;
    out.indices[out.index_count++] = base + 2;
    out.indices[out.index_count++] = base + 3;
}

SelectionBoundsGeometry build_selection_bounds_geometry(
    const nw::render::Bounds& bounds,
    const glm::vec3& camera_position,
    const glm::vec4& color)
{
    SelectionBoundsGeometry result;
    if (!finite_ordered_bounds(bounds)) {
        return result;
    }

    constexpr float kBoundsPadding = 0.04f;
    const glm::vec3 minimum = bounds.min - glm::vec3{kBoundsPadding};
    const glm::vec3 maximum = bounds.max + glm::vec3{kBoundsPadding};
    std::array<glm::vec3, 8> corners;
    for (uint32_t corner = 0; corner < corners.size(); ++corner) {
        corners[corner] = {
            (corner & 1u) != 0u ? maximum.x : minimum.x,
            (corner & 2u) != 0u ? maximum.y : minimum.y,
            (corner & 4u) != 0u ? maximum.z : minimum.z,
        };
    }

    constexpr std::array<std::array<uint8_t, 2>, kSelectionBoundsEdgeCount> edges{{
        {{0, 1}},
        {{2, 3}},
        {{4, 5}},
        {{6, 7}},
        {{0, 2}},
        {{1, 3}},
        {{4, 6}},
        {{5, 7}},
        {{0, 4}},
        {{1, 5}},
        {{2, 6}},
        {{3, 7}},
    }};
    for (const auto& edge : edges) {
        append_selection_bounds_edge(result, corners[edge[0]], corners[edge[1]], camera_position, color);
    }
    return result;
}

} // namespace

void SceneDebugRenderer::render_debug_grid(nw::gfx::CommandList* cmd, const PreviewScene& scene, const nw::render::RenderContext& ctx,
    float spacing, float major_interval, float minor_width, float major_width, float opacity, float z_offset)
{
    if (!cmd || !debug_grid_pipeline_.valid()) {
        return;
    }

    // Keep the debug grid anchored to the authored scene bounds instead of
    // live particle bounds so it does not drift as effects expand and contract.
    const auto bounds = scene.bounds;
    const glm::vec3 center = bounds.center();
    const float radius = std::max(bounds.radius(), 1.0f);
    const float half_extent = std::max(radius * 4.0f, 20.0f);
    const float plane_z = (scene.is_area ? scene.area_overlay_z : bounds.min.z) + z_offset;
    spacing = std::max(spacing, 0.01f);
    major_interval = std::max(major_interval, 1.0f);
    minor_width = std::clamp(minor_width, 0.0f, 1.0f);
    major_width = std::clamp(major_width, 0.0f, 1.0f);
    opacity = std::clamp(opacity, 0.0f, 1.0f);

    std::vector<DebugGridVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(4);
    indices = {0, 1, 2, 0, 2, 3};
    vertices.push_back(DebugGridVertex{
        {center.x - half_extent, center.y - half_extent, plane_z},
    });
    vertices.push_back(DebugGridVertex{
        {center.x + half_extent, center.y - half_extent, plane_z},
    });
    vertices.push_back(DebugGridVertex{
        {center.x + half_extent, center.y + half_extent, plane_z},
    });
    vertices.push_back(DebugGridVertex{
        {center.x - half_extent, center.y + half_extent, plane_z},
    });

    nw::gfx::BufferDesc vb_desc{};
    vb_desc.size = vertices.size() * sizeof(DebugGridVertex);
    vb_desc.usage = nw::gfx::BufferUsage::Vertex;
    vb_desc.cpu_visible = true;
    if (!debug_grid_vertices_.valid() || debug_grid_vertex_capacity_ < vertices.size()) {
        if (debug_grid_vertices_.valid()) {
            nw::gfx::destroy_buffer(debug_grid_vertices_);
        }
        debug_grid_vertices_ = nw::gfx::create_buffer(ctx_, vb_desc);
        debug_grid_vertex_capacity_ = vertices.size();
    }

    nw::gfx::BufferDesc ib_desc{};
    ib_desc.size = indices.size() * sizeof(uint32_t);
    ib_desc.usage = nw::gfx::BufferUsage::Index;
    ib_desc.cpu_visible = true;
    if (!debug_grid_indices_.valid() || debug_grid_index_capacity_ < indices.size()) {
        if (debug_grid_indices_.valid()) {
            nw::gfx::destroy_buffer(debug_grid_indices_);
        }
        debug_grid_indices_ = nw::gfx::create_buffer(ctx_, ib_desc);
        debug_grid_index_capacity_ = indices.size();
    }

    if (!debug_grid_vertices_.valid() || !debug_grid_indices_.valid()) {
        return;
    }

    auto* vp = nw::gfx::map_buffer(debug_grid_vertices_);
    auto* ip = nw::gfx::map_buffer(debug_grid_indices_);
    if (!vp || !ip) {
        if (vp) nw::gfx::unmap_buffer(debug_grid_vertices_);
        if (ip) nw::gfx::unmap_buffer(debug_grid_indices_);
        return;
    }
    std::memcpy(vp, vertices.data(), vb_desc.size);
    std::memcpy(ip, indices.data(), ib_desc.size);
    nw::gfx::unmap_buffer(debug_grid_vertices_);
    nw::gfx::unmap_buffer(debug_grid_indices_);

    DebugGridConstants sc{};
    sc.view = ctx.view;
    sc.projection = ctx.projection;
    sc.minor_color = glm::vec4{0.72f, 0.78f, 0.90f, opacity * 0.45f};
    sc.major_color = glm::vec4{0.92f, 0.96f, 1.0f, opacity};
    sc.grid_params = glm::vec4{spacing, major_interval, minor_width, major_width};

    auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(DebugGridConstants));
    if (uniforms.data) {
        std::memcpy(uniforms.data, &sc, sizeof(DebugGridConstants));
        nw::gfx::cmd_bind_pipeline(cmd, debug_grid_pipeline_);
        nw::gfx::cmd_bind_vertex_buffer(cmd, debug_grid_vertices_, sizeof(DebugGridVertex));
        nw::gfx::cmd_bind_index_buffer(cmd, debug_grid_indices_, sizeof(uint32_t));
        nw::gfx::cmd_bind_resources(cmd, debug_grid_pipeline_, uniforms);
        nw::gfx::cmd_draw_indexed(cmd, static_cast<uint32_t>(indices.size()));
    }
}

namespace {

bool debug_shape_category_enabled(DebugShapeCategory category, DebugShapeOptions options) noexcept
{
    switch (category) {
    case DebugShapeCategory::general:
        return true;
    case DebugShapeCategory::trigger:
        return options.triggers;
    case DebugShapeCategory::encounter:
        return options.encounters;
    }
    return true;
}

std::vector<glm::vec3> normalize_debug_polygon_points(
    std::span<const glm::vec3> geometry,
    const glm::mat4& placement,
    float z_offset)
{
    std::vector<glm::vec3> points;
    points.reserve(geometry.size());
    for (const auto& local_point : geometry) {
        auto point = glm::vec3(placement * glm::vec4{local_point, 1.0f});
        point.z += z_offset;
        if (!points.empty() && glm::length(glm::vec2{point.x - points.back().x, point.y - points.back().y}) <= 1.0e-5f) {
            continue;
        }
        points.push_back(point);
    }

    if (points.size() > 1) {
        const auto& first = points.front();
        const auto& last = points.back();
        if (glm::length(glm::vec2{first.x - last.x, first.y - last.y}) <= 1.0e-5f) {
            points.pop_back();
        }
    }
    return points;
}

void append_debug_polygon_outline(
    PreviewScene& scene,
    std::span<const glm::vec3> points,
    const glm::vec4& outline_color,
    float outline_width)
{
    if (points.size() < 2) {
        return;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        append_debug_segment(scene, points[i], points[(i + 1) % points.size()], outline_color, outline_width);
    }
}

bool append_debug_shape_selection_range(
    PreviewScene& scene,
    DebugShapeCategory category,
    nw::ObjectHandle object,
    uint32_t debug_shape_range_index,
    std::span<const glm::vec3> polygon)
{
    if (!nw::kernel::objects().valid(object)
        || debug_shape_range_index >= scene.debug_shape_ranges.size()
        || scene.debug_shape_selection_ranges.size() >= kInvalidAreaRenderRecordIndex
        || scene.debug_shape_selection_points.size() > std::numeric_limits<uint32_t>::max()
        || polygon.size() > std::numeric_limits<uint32_t>::max() - scene.debug_shape_selection_points.size()) {
        return false;
    }

    nw::render::Bounds bounds{};
    bool has_bounds = false;
    const auto include_point = [&bounds, &has_bounds](const glm::vec3& point) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            return false;
        }
        if (!has_bounds) {
            bounds = {.min = point, .max = point};
            has_bounds = true;
        } else {
            bounds.min = glm::min(bounds.min, point);
            bounds.max = glm::max(bounds.max, point);
        }
        return true;
    };

    const uint32_t first_point = static_cast<uint32_t>(scene.debug_shape_selection_points.size());
    float plane_z = 0.0f;
    const bool polygon_selection = polygon.size() >= 3;
    if (polygon_selection) {
        for (size_t point_index = 0; point_index < polygon.size(); ++point_index) {
            const auto& point = polygon[point_index];
            if (!include_point(point)) {
                return false;
            }
            plane_z += (point.z - plane_z) / static_cast<float>(point_index + 1);
        }
        scene.debug_shape_selection_points.insert(
            scene.debug_shape_selection_points.end(), polygon.begin(), polygon.end());
    } else {
        bounds = {};
        has_bounds = false;
        const auto& debug_range = scene.debug_shape_ranges[debug_shape_range_index];
        const size_t first_index = debug_range.first_index;
        const size_t index_end = std::min<size_t>(
            first_index + debug_range.index_count,
            scene.debug_shape_indices.size());
        for (size_t index = first_index; index < index_end; ++index) {
            const uint32_t vertex_index = scene.debug_shape_indices[index];
            if (vertex_index >= scene.debug_shape_vertices.size()
                || !include_point(scene.debug_shape_vertices[vertex_index].position)) {
                return false;
            }
        }
    }
    if (!has_bounds) {
        return false;
    }

    scene.debug_shape_selection_ranges.push_back(DebugShapeSelectionRange{
        .bounds = bounds,
        .object = object,
        .debug_shape_range_index = debug_shape_range_index,
        .first_point = first_point,
        .point_count = polygon_selection ? static_cast<uint32_t>(polygon.size()) : 0u,
        .plane_z = plane_z,
        .category = category,
    });
    return true;
}

void append_debug_spawn_marker(PreviewScene& scene, const nw::ObjectSpawnPoint& spawn_point)
{
    constexpr float k_marker_z_offset = 0.16f;
    constexpr float k_marker_width = 0.08f;
    const glm::vec4 color{1.0f, 0.22f, 0.18f, 0.95f};
    const glm::vec3 origin = spawn_point.position + glm::vec3{0.0f, 0.0f, k_marker_z_offset};
    const glm::vec3 forward{std::cos(spawn_point.orientation), std::sin(spawn_point.orientation), 0.0f};
    const glm::vec3 right{-forward.y, forward.x, 0.0f};

    append_debug_segment(scene, origin - forward * 0.25f, origin + forward * 0.65f, color, k_marker_width);
    append_debug_segment(scene, origin - right * 0.25f, origin + right * 0.25f, color, k_marker_width);
    append_debug_triangle(
        scene,
        origin + forward * 0.85f,
        origin + forward * 0.5f + right * 0.2f,
        origin + forward * 0.5f - right * 0.2f,
        color);
}

} // namespace

void append_debug_triangle(
    PreviewScene& scene,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec4& color)
{
    const auto base = static_cast<uint32_t>(scene.debug_shape_vertices.size());
    scene.debug_shape_vertices.push_back({a, color});
    scene.debug_shape_vertices.push_back({b, color});
    scene.debug_shape_vertices.push_back({c, color});
    scene.debug_shape_indices.push_back(base);
    scene.debug_shape_indices.push_back(base + 1);
    scene.debug_shape_indices.push_back(base + 2);
}

void append_debug_segment(
    PreviewScene& scene,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec4& color,
    float width)
{
    const glm::vec2 delta{b.x - a.x, b.y - a.y};
    const float length = glm::length(delta);
    if (length <= 1.0e-5f) {
        return;
    }

    const glm::vec2 side = glm::vec2{-delta.y, delta.x} * (0.5f * width / length);
    const auto base = static_cast<uint32_t>(scene.debug_shape_vertices.size());
    scene.debug_shape_vertices.push_back({{a.x + side.x, a.y + side.y, a.z}, color});
    scene.debug_shape_vertices.push_back({{b.x + side.x, b.y + side.y, b.z}, color});
    scene.debug_shape_vertices.push_back({{b.x - side.x, b.y - side.y, b.z}, color});
    scene.debug_shape_vertices.push_back({{a.x - side.x, a.y - side.y, a.z}, color});
    scene.debug_shape_indices.insert(scene.debug_shape_indices.end(), {
                                                                          base,
                                                                          base + 1,
                                                                          base + 2,
                                                                          base,
                                                                          base + 2,
                                                                          base + 3,
                                                                      });
}

uint32_t append_debug_shape_range(PreviewScene& scene, DebugShapeCategory category, size_t first_index)
{
    const size_t last_index = scene.debug_shape_indices.size();
    if (last_index <= first_index
        || first_index > std::numeric_limits<uint32_t>::max()
        || scene.debug_shape_ranges.size() >= kInvalidAreaRenderRecordIndex) {
        return kInvalidAreaRenderRecordIndex;
    }

    const uint32_t range_index = static_cast<uint32_t>(scene.debug_shape_ranges.size());
    scene.debug_shape_ranges.push_back(DebugShapeRange{
        .category = category,
        .first_index = static_cast<uint32_t>(first_index),
        .index_count = static_cast<uint32_t>(std::min<size_t>(
            last_index - first_index,
            std::numeric_limits<uint32_t>::max())),
    });
    return range_index;
}

bool append_trigger_debug_geometry(PreviewScene& scene, const nw::Trigger& trigger)
{
    const auto* geometry = nw::kernel::objects().components().find_geometry(trigger.handle());
    if (!geometry || geometry->points.empty()) {
        return false;
    }

    const size_t first_debug_index = scene.debug_shape_indices.size();
    constexpr float k_floor_z_offset = 0.08f;
    constexpr float k_outline_width = 0.07f;
    const glm::vec4 outline_color{0.12f, 0.92f, 1.0f, 0.9f};
    const glm::mat4 placement = area_object_placement_transform(object_spatial_location(trigger));
    const auto floor_points = normalize_debug_polygon_points(geometry->points, placement, k_floor_z_offset);
    append_debug_polygon_outline(scene, floor_points, outline_color, k_outline_width);

    if (geometry->highlight_height > 0.25f) {
        const auto highlight_points = normalize_debug_polygon_points(
            geometry->points,
            placement,
            k_floor_z_offset + geometry->highlight_height);
        append_debug_polygon_outline(
            scene, highlight_points, {0.12f, 0.92f, 1.0f, 0.55f}, k_outline_width * 0.8f);
    }
    const uint32_t debug_range_index = append_debug_shape_range(
        scene, DebugShapeCategory::trigger, first_debug_index);
    append_debug_shape_selection_range(
        scene, DebugShapeCategory::trigger, trigger.handle(), debug_range_index, floor_points);
    return scene.debug_shape_indices.size() > first_debug_index;
}

bool append_encounter_debug_geometry(PreviewScene& scene, const nw::Encounter& encounter)
{
    const size_t first_debug_index = scene.debug_shape_indices.size();
    const auto* geometry = nw::kernel::objects().components().find_geometry(encounter.handle());
    std::vector<glm::vec3> floor_points;
    if (geometry && !geometry->points.empty()) {
        constexpr float k_floor_z_offset = 0.12f;
        constexpr float k_outline_width = 0.07f;
        floor_points = normalize_debug_polygon_points(
            geometry->points,
            area_object_placement_transform(object_spatial_location(encounter)),
            k_floor_z_offset);
        append_debug_polygon_outline(
            scene, floor_points, {1.0f, 0.18f, 0.72f, 0.9f}, k_outline_width);
    }

    const uint32_t footprint_range_index = append_debug_shape_range(
        scene, DebugShapeCategory::encounter, first_debug_index);
    if (floor_points.size() >= 3
        && footprint_range_index != kInvalidAreaRenderRecordIndex) {
        append_debug_shape_selection_range(
            scene,
            DebugShapeCategory::encounter,
            encounter.handle(),
            footprint_range_index,
            floor_points);
    }

    const size_t first_spawn_index = scene.debug_shape_indices.size();
    if (geometry) {
        for (const auto& spawn_point : geometry->spawn_points) {
            append_debug_spawn_marker(scene, spawn_point);
        }
    }
    append_debug_shape_range(
        scene, DebugShapeCategory::encounter, first_spawn_index);
    return scene.debug_shape_indices.size() > first_debug_index;
}

void SceneDebugRenderer::render_debug_shapes(
    nw::gfx::CommandList* cmd, const PreviewScene& scene, const nw::render::RenderContext& ctx, DebugShapeOptions options)
{
    if (!options.enabled || !cmd || !debug_shape_pipeline_.valid()
        || scene.debug_shape_vertices.empty() || scene.debug_shape_indices.empty()) {
        return;
    }

    const uint32_t* index_data = scene.debug_shape_indices.data();
    size_t index_count = scene.debug_shape_indices.size();
    std::vector<uint32_t> filtered_indices;
    const bool filter_by_category = (!options.triggers || !options.encounters) && !scene.debug_shape_ranges.empty();
    if (filter_by_category) {
        filtered_indices.reserve(scene.debug_shape_indices.size());
        for (const auto& range : scene.debug_shape_ranges) {
            if (!debug_shape_category_enabled(range.category, options)) {
                continue;
            }

            const size_t first = range.first_index;
            if (first >= scene.debug_shape_indices.size()) {
                continue;
            }
            const size_t count = std::min<size_t>(range.index_count, scene.debug_shape_indices.size() - first);
            filtered_indices.insert(
                filtered_indices.end(),
                scene.debug_shape_indices.begin() + static_cast<std::ptrdiff_t>(first),
                scene.debug_shape_indices.begin() + static_cast<std::ptrdiff_t>(first + count));
        }
        if (filtered_indices.empty()) {
            return;
        }
        index_data = filtered_indices.data();
        index_count = filtered_indices.size();
    }

    nw::gfx::BufferDesc vb_desc{};
    vb_desc.size = scene.debug_shape_vertices.size() * sizeof(DebugShapeVertex);
    vb_desc.usage = nw::gfx::BufferUsage::Vertex;
    vb_desc.cpu_visible = true;
    if (!debug_shape_vertices_.valid() || debug_shape_vertex_capacity_ < scene.debug_shape_vertices.size()) {
        if (debug_shape_vertices_.valid()) {
            nw::gfx::destroy_buffer(debug_shape_vertices_);
        }
        debug_shape_vertices_ = nw::gfx::create_buffer(ctx_, vb_desc);
        debug_shape_vertex_capacity_ = scene.debug_shape_vertices.size();
    }

    nw::gfx::BufferDesc ib_desc{};
    ib_desc.size = index_count * sizeof(uint32_t);
    ib_desc.usage = nw::gfx::BufferUsage::Index;
    ib_desc.cpu_visible = true;
    if (!debug_shape_indices_.valid() || debug_shape_index_capacity_ < index_count) {
        if (debug_shape_indices_.valid()) {
            nw::gfx::destroy_buffer(debug_shape_indices_);
        }
        debug_shape_indices_ = nw::gfx::create_buffer(ctx_, ib_desc);
        debug_shape_index_capacity_ = index_count;
    }

    if (!debug_shape_vertices_.valid() || !debug_shape_indices_.valid()) {
        return;
    }

    auto* vp = nw::gfx::map_buffer(debug_shape_vertices_);
    auto* ip = nw::gfx::map_buffer(debug_shape_indices_);
    if (!vp || !ip) {
        if (vp) nw::gfx::unmap_buffer(debug_shape_vertices_);
        if (ip) nw::gfx::unmap_buffer(debug_shape_indices_);
        return;
    }
    std::memcpy(vp, scene.debug_shape_vertices.data(), vb_desc.size);
    std::memcpy(ip, index_data, ib_desc.size);
    nw::gfx::unmap_buffer(debug_shape_vertices_);
    nw::gfx::unmap_buffer(debug_shape_indices_);

    DebugShapeConstants sc{};
    sc.view = ctx.view;
    sc.projection = ctx.projection;

    auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(DebugShapeConstants));
    if (uniforms.data) {
        std::memcpy(uniforms.data, &sc, sizeof(DebugShapeConstants));
        nw::gfx::cmd_bind_pipeline(cmd, debug_shape_pipeline_);
        nw::gfx::cmd_bind_vertex_buffer(cmd, debug_shape_vertices_, sizeof(DebugShapeVertex));
        nw::gfx::cmd_bind_index_buffer(cmd, debug_shape_indices_, sizeof(uint32_t));
        nw::gfx::cmd_bind_resources(cmd, debug_shape_pipeline_, uniforms);
        nw::gfx::cmd_draw_indexed(cmd, static_cast<uint32_t>(index_count));
    }
}

void SceneDebugRenderer::render_selection_bounds(
    nw::gfx::CommandList* cmd,
    const nw::render::Bounds& bounds,
    const nw::render::RenderContext& ctx,
    const glm::vec4& color)
{
    if (!cmd || !debug_shape_pipeline_.valid()) {
        return;
    }

    const auto geometry = build_selection_bounds_geometry(bounds, ctx.camera_position, color);
    if (geometry.index_count == 0) {
        return;
    }

    if (!selection_bounds_vertices_.valid()) {
        selection_bounds_vertices_ = nw::gfx::create_buffer(ctx_, {
                                                                      .size = kSelectionBoundsVertexCount * sizeof(DebugShapeVertex),
                                                                      .usage = nw::gfx::BufferUsage::Vertex,
                                                                      .cpu_visible = true,
                                                                  });
    }
    if (!selection_bounds_indices_.valid()) {
        selection_bounds_indices_ = nw::gfx::create_buffer(ctx_, {
                                                                     .size = kSelectionBoundsIndexCount * sizeof(uint32_t),
                                                                     .usage = nw::gfx::BufferUsage::Index,
                                                                     .cpu_visible = true,
                                                                 });
    }
    if (!selection_bounds_vertices_.valid() || !selection_bounds_indices_.valid()) {
        return;
    }

    auto* vertex_data = nw::gfx::map_buffer(selection_bounds_vertices_);
    auto* index_data = nw::gfx::map_buffer(selection_bounds_indices_);
    if (!vertex_data || !index_data) {
        if (vertex_data) nw::gfx::unmap_buffer(selection_bounds_vertices_);
        if (index_data) nw::gfx::unmap_buffer(selection_bounds_indices_);
        return;
    }
    std::memcpy(vertex_data, geometry.vertices.data(), geometry.vertex_count * sizeof(DebugShapeVertex));
    std::memcpy(index_data, geometry.indices.data(), geometry.index_count * sizeof(uint32_t));
    nw::gfx::unmap_buffer(selection_bounds_vertices_);
    nw::gfx::unmap_buffer(selection_bounds_indices_);

    DebugShapeConstants constants{};
    constants.view = ctx.view;
    constants.projection = ctx.projection;
    auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(DebugShapeConstants));
    if (!uniforms.data) {
        return;
    }
    std::memcpy(uniforms.data, &constants, sizeof(DebugShapeConstants));
    nw::gfx::cmd_bind_pipeline(cmd, debug_shape_pipeline_);
    nw::gfx::cmd_bind_vertex_buffer(cmd, selection_bounds_vertices_, sizeof(DebugShapeVertex));
    nw::gfx::cmd_bind_index_buffer(cmd, selection_bounds_indices_, sizeof(uint32_t));
    nw::gfx::cmd_bind_resources(cmd, debug_shape_pipeline_, uniforms);
    nw::gfx::cmd_draw_indexed(cmd, geometry.index_count);
}

} // namespace nw::render::viewer
