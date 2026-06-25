#include "fog_renderer.hpp"

#include "shader_provider.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace nw::render {

namespace {

struct FogCompositeVertex {
    glm::vec2 position;
    glm::vec2 texcoord;
};

glm::vec3 linear_to_display_gamma(const glm::vec3& color)
{
    return glm::pow(glm::clamp(color, glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec3{1.0f / 2.2f});
}

} // namespace

FogRenderer::FogRenderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

FogRenderer::~FogRenderer()
{
    if (render_target_.valid()) nw::gfx::destroy_render_target(ctx_, render_target_);
    if (vertices_.valid()) nw::gfx::destroy_buffer(vertices_);
    if (pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pipeline_);
}

bool FogRenderer::initialize(ShaderProvider& shader_provider)
{
    if (!ctx_) {
        return false;
    }

    auto vs_fog = shader_provider.get_shader("render_fog_composite.vs.hlsl");
    auto ps_fog = shader_provider.get_shader("render_fog_composite.ps.hlsl");
    if (!vs_fog.valid() || !ps_fog.valid()) {
        return true;
    }

    nw::gfx::PipelineDesc desc{};
    desc.vs = vs_fog;
    desc.fs = ps_fog;
    desc.uses_bindless_sampled_textures = true;
    desc.uses_single_texture = false;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.vertex_stride = sizeof(FogCompositeVertex);
    desc.vertex_attributes = {
        {0, offsetof(FogCompositeVertex, position), nw::gfx::VertexFormat::Float2},
        {1, offsetof(FogCompositeVertex, texcoord), nw::gfx::VertexFormat::Float2},
    };
    pipeline_ = nw::gfx::create_pipeline(ctx_, desc);
    if (!pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create fog composite pipeline");
        return false;
    }

    constexpr std::array<FogCompositeVertex, 3> kFullscreenTriangle{{
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{-1.0f, 3.0f}, {0.0f, 2.0f}},
        {{3.0f, -1.0f}, {2.0f, 0.0f}},
    }};
    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = sizeof(kFullscreenTriangle);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    vertices_ = nw::gfx::create_buffer(ctx_, vertex_desc);
    if (!vertices_.valid()) {
        LOG_F(ERROR, "Failed to create fog composite vertex buffer");
        return false;
    }
    auto* mapped = nw::gfx::map_buffer(vertices_);
    if (!mapped) {
        LOG_F(ERROR, "Failed to map fog composite vertex buffer");
        return false;
    }
    std::memcpy(mapped, kFullscreenTriangle.data(), sizeof(kFullscreenTriangle));
    nw::gfx::unmap_buffer(vertices_);
    return true;
}

bool FogRenderer::ensure_resources(uint32_t width, uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (render_target_.valid() && target_width_ == width && target_height_ == height) {
        return true;
    }

    if (render_target_.valid()) {
        nw::gfx::wait_idle(ctx_);
        nw::gfx::destroy_render_target(ctx_, render_target_);
        render_target_ = {};
        color_texture_ = {};
        depth_texture_ = {};
        target_width_ = 0;
        target_height_ = 0;
    }

    nw::gfx::TextureDesc color_desc{};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.format = nw::gfx::Fmt::RGBA16F;
    color_desc.sampled = true;
    color_desc.render_target = true;
    color_texture_ = nw::gfx::create_texture(ctx_, color_desc);
    if (!color_texture_.valid()) {
        LOG_F(ERROR, "Failed to create fog offscreen color target");
        return false;
    }

    nw::gfx::TextureDesc depth_desc{};
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.format = nw::gfx::Fmt::D32F;
    depth_desc.sampled = true;
    depth_desc.render_target = true;
    depth_texture_ = nw::gfx::create_texture(ctx_, depth_desc);
    if (!depth_texture_.valid()) {
        LOG_F(ERROR, "Failed to create fog offscreen depth target");
        nw::gfx::destroy_texture(ctx_, color_texture_);
        color_texture_ = {};
        return false;
    }

    nw::gfx::RenderTargetDesc rt_desc{};
    rt_desc.color[0].texture = color_texture_;
    rt_desc.depth.texture = depth_texture_;
    render_target_ = nw::gfx::create_render_target(ctx_, rt_desc);
    if (!render_target_.valid()) {
        LOG_F(ERROR, "Failed to create fog offscreen render target");
        nw::gfx::destroy_texture(ctx_, depth_texture_);
        nw::gfx::destroy_texture(ctx_, color_texture_);
        depth_texture_ = {};
        color_texture_ = {};
        return false;
    }

    target_width_ = width;
    target_height_ = height;
    return true;
}

void FogRenderer::render_composite(nw::gfx::CommandList* cmd, const RenderContext& ctx, const SceneFog& fog)
{
    if (!cmd || !fog.enabled || !pipeline_.valid() || !vertices_.valid()
        || !color_texture_.valid() || !depth_texture_.valid()) {
        return;
    }

    const auto color_index = nw::gfx::get_bindless_texture_index(ctx_, color_texture_);
    const auto depth_index = nw::gfx::get_bindless_texture_index(ctx_, depth_texture_);
    if (!nw::gfx::bindless_texture_index_valid(color_index)
        || !nw::gfx::bindless_texture_index_valid(depth_index)) {
        return;
    }

    auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(FogCompositeConstants));
    if (!uniforms.data) {
        return;
    }

    FogCompositeConstants constants{};
    constants.color_texture_index = color_index;
    constants.depth_texture_index = depth_index;
    constants.fog_amount = fog.amount;
    constants.fog_color = glm::vec4(linear_to_display_gamma(fog.color), 0.0f);
    constants.fog_range = glm::vec2(fog.start_distance, fog.end_distance);
    constants.camera_clip_planes = glm::vec2(ctx.camera_near_plane, ctx.camera_far_plane);
    std::memcpy(uniforms.data, &constants, sizeof(constants));

    nw::gfx::cmd_bind_pipeline(cmd, pipeline_);
    nw::gfx::cmd_bind_vertex_buffer(cmd, vertices_, sizeof(FogCompositeVertex));
    nw::gfx::cmd_bind_resources(cmd, pipeline_, uniforms);
    nw::gfx::cmd_draw(cmd, 3);
}

} // namespace nw::render
