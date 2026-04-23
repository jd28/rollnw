#include "renderer.hpp"

#include "preview_scene.hpp"

#include <nw/formats/Image.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/render/nwn/model_gpu_backend.hpp>
#include <nw/render/nwn/model_renderer.hpp>
#include <nw/log.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace mudl {

Renderer::Renderer(nw::gfx::Context* ctx)
    : ctx_(ctx)
{
}

Renderer::~Renderer()
{
    if (debug_grid_indices_.valid()) nw::gfx::destroy_buffer(debug_grid_indices_);
    if (debug_grid_vertices_.valid()) nw::gfx::destroy_buffer(debug_grid_vertices_);
    if (debug_grid_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, debug_grid_pipeline_);
}

bool Renderer::initialize(nw::render::ShaderProvider* shader_provider)
{
    shader_provider_ = shader_provider;
    auto* service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!service) {
        LOG_F(ERROR, "Render service is not registered");
        return false;
    }
    if (!service->ensure_initialized()) {
        LOG_F(ERROR, "Render service is not initialized");
        return false;
    }
    render_service_ = service;

    auto vs_debug = shader_provider_->get_shader("render_debug_grid.vs.hlsl");
    auto ps_debug = shader_provider_->get_shader("render_debug_grid.ps.hlsl");
    if (vs_debug.valid() && ps_debug.valid()) {
        nw::gfx::PipelineDesc debug_desc{};
        debug_desc.vs = vs_debug;
        debug_desc.fs = ps_debug;
        debug_desc.uses_single_texture = false;
        debug_desc.depth_test = true;
        debug_desc.depth_write = false;
        debug_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
        debug_desc.vertex_stride = sizeof(Vertex);
        debug_desc.vertex_attributes = {
            {0, offsetof(Vertex, position), nw::gfx::VertexFormat::Float3},
            {1, offsetof(Vertex, normal), nw::gfx::VertexFormat::Float3},
            {2, offsetof(Vertex, texcoord), nw::gfx::VertexFormat::Float2},
            {3, offsetof(Vertex, tangent), nw::gfx::VertexFormat::Float4},
        };
        debug_grid_pipeline_ = nw::gfx::create_pipeline(ctx_, debug_desc);
        if (!debug_grid_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create debug grid pipeline");
        }
    }

    LOG_F(INFO, "Renderer initialized");
    return true;
}

nw::render::Renderer& Renderer::render_backend() const
{
    return render_service_->renderer();
}

ModelGpuBackend& Renderer::model_backend() const
{
    return render_service_->model_backend();
}

RenderAssetCache& Renderer::asset_cache() const
{
    return render_service_->asset_cache();
}

nw::gfx::Handle<nw::gfx::Texture> Renderer::get_or_load_texture(const std::string& name, bool premultiply_alpha)
{
    return asset_cache().get_or_load_texture(name, premultiply_alpha, model_backend().fallback_texture());
}

nw::gfx::Handle<nw::gfx::Texture> Renderer::get_or_load_texture(const std::string& name, const nw::PltColors& colors, bool premultiply_alpha)
{
    return asset_cache().get_or_load_texture(name, colors, premultiply_alpha, model_backend().fallback_texture());
}

nw::gfx::Handle<nw::gfx::Texture> Renderer::get_or_load_raw_plt_texture(const std::string& name)
{
    return asset_cache().get_or_load_raw_plt_texture(name);
}

const nw::Image* Renderer::get_or_load_source_image(const std::string& name)
{
    return asset_cache().get_or_load_source_image(name);
}

nw::gfx::Handle<nw::gfx::Texture> Renderer::get_texture(std::string_view name, bool premultiply_alpha)
{
    return get_or_load_texture(std::string{name}, premultiply_alpha);
}

const nw::Image* Renderer::get_source_image(std::string_view name)
{
    return get_or_load_source_image(std::string{name});
}

ModelInstance* Renderer::get_or_load_particle_mesh(const std::string& resref)
{
    return asset_cache().get_or_load_particle_mesh(resref);
}

void Renderer::render_model(nw::gfx::CommandList* cmd, const ModelInstance& model, const RenderContext& ctx,
    RenderPassSelection pass)
{
    nw::render::nwn::ModelRenderContext model_render_ctx{ctx_, &model_backend(), &asset_cache()};
    nw::render::nwn::render_model_instance(model_render_ctx, cmd, model, ctx, pass);
}

void Renderer::render_static_model(nw::gfx::CommandList* cmd, const nw::render::RenderModel& model,
    const RenderContext& ctx, RenderPassSelection pass, const std::vector<std::vector<glm::mat4>>* skin_matrices)
{
    render_backend().render_static_model(cmd, model, ctx, pass, skin_matrices);
}

void Renderer::render_shadow_model(nw::gfx::CommandList* cmd, const ModelInstance& model,
    const glm::mat4& light_view, const glm::mat4& light_projection)
{
    nw::render::nwn::ModelRenderContext model_render_ctx{ctx_, &model_backend(), &asset_cache()};
    nw::render::nwn::render_shadow_model_instance(model_render_ctx, cmd, model, light_view, light_projection);
}

void Renderer::render_particles(nw::gfx::CommandList* cmd, const PreviewScene& scene, const RenderContext& ctx)
{
    if (!cmd) {
        return;
    }

    for (const auto& scene_particles : scene.particles) {
        const auto* effect = scene_particles.system.effect;
        if (!effect) {
            continue;
        }

        const auto packets = scene_particles.system.render_packets.span();
        const auto& core = scene_particles.system.particles.core;
        for (const auto& packet : packets) {
            if (packet.material >= effect->materials.size()) {
                continue;
            }

            const auto& source_material = packet.material < scene_particles.import.effect.materials.size()
                ? scene_particles.import.effect.materials[packet.material]
                : nw::render::ParticleMaterialDef{};
            if (packet.mode == nw::render::ParticleRenderMode::mesh) {
                if (source_material.mesh.empty()) {
                    continue;
                }
                auto* chunk_model = get_or_load_particle_mesh(source_material.mesh);
                if (!chunk_model || chunk_model->nodes_.empty()) {
                    continue;
                }

                for (uint32_t n = 0; n < packet.count; ++n) {
                    const size_t i = packet.begin + n;
                    if (i >= core.position.size()) {
                        break;
                    }

                    const float sx = std::max(core.size_x[i], 0.001f);
                    const float sy = std::max(core.size_y[i], 0.001f);
                    const float uniform_scale = std::max(0.5f * (sx + sy), 0.001f);
                    const float angle = core.rotation[i] * 2.0f * 3.14159265358979323846f;
                    glm::mat4 model_root = glm::translate(glm::mat4(1.0f), core.position[i]);
                    model_root = model_root * glm::mat4_cast(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
                    model_root = model_root * glm::scale(glm::mat4(1.0f), glm::vec3(uniform_scale));
                    nw::render::nwn::ModelRenderContext model_render_ctx{ctx_, &model_backend(), &asset_cache()};
                    nw::render::nwn::render_model_instance_with_root(model_render_ctx, cmd, *chunk_model, model_root, ctx);
                }
            }
        }

        render_backend().render_particles(
            cmd,
            scene_particles.system,
            scene_particles.import.effect.materials,
            ctx,
            *this);
    }
}

namespace {

struct DebugGridConstants {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec4 minor_color{0.0f};
    glm::vec4 major_color{0.0f};
    glm::vec4 grid_params{1.0f, 10.0f, 0.03f, 0.08f};
};

} // namespace

void Renderer::render_debug_grid(nw::gfx::CommandList* cmd, const PreviewScene& scene, const RenderContext& ctx,
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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(4);
    indices = {0, 1, 2, 0, 2, 3};
    vertices.push_back(Vertex{
        {center.x - half_extent, center.y - half_extent, plane_z},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
    });
    vertices.push_back(Vertex{
        {center.x + half_extent, center.y - half_extent, plane_z},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
    });
    vertices.push_back(Vertex{
        {center.x + half_extent, center.y + half_extent, plane_z},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
    });
    vertices.push_back(Vertex{
        {center.x - half_extent, center.y + half_extent, plane_z},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
    });

    nw::gfx::BufferDesc vb_desc{};
    vb_desc.size = vertices.size() * sizeof(Vertex);
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
        nw::gfx::cmd_bind_vertex_buffer(cmd, debug_grid_vertices_, sizeof(Vertex));
        nw::gfx::cmd_bind_index_buffer(cmd, debug_grid_indices_, sizeof(uint32_t));
        nw::gfx::cmd_bind_resources(cmd, debug_grid_pipeline_, uniforms);
        nw::gfx::cmd_draw_indexed(cmd, static_cast<uint32_t>(indices.size()));
    }
}

} // namespace mudl
