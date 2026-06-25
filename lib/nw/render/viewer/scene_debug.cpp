#include "scene_debug.hpp"

#include "preview_scene.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/log.hpp>
#include <nw/render/render_context.hpp>
#include <nw/render/shader_provider.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace nw::render::viewer {

namespace {

struct DebugGridVertex {
    glm::vec3 position{0.0f};
};

} // namespace

SceneDebugRenderer::SceneDebugRenderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

SceneDebugRenderer::~SceneDebugRenderer()
{
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

} // namespace

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

} // namespace nw::render::viewer
