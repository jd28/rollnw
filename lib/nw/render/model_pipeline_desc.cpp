#include "model_pipeline_desc.hpp"

#include <nw/render/model.hpp>

#include <cstddef>

namespace nw::render {

nw::gfx::PipelineDesc make_offscreen_pipeline_desc(const nw::gfx::PipelineDesc& base)
{
    auto desc = base;
    desc.use_swapchain_color_format = false;
    desc.color_attachment_format = nw::gfx::Fmt::RGBA16F;
    desc.depth_attachment_format = nw::gfx::Fmt::D32F;
    return desc;
}

nw::gfx::PipelineDesc make_transparent_pipeline_desc(const nw::gfx::PipelineDesc& base)
{
    auto desc = base;
    desc.depth_write = false;
    desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
    return desc;
}

nw::gfx::PipelineDesc make_shader_variant_pipeline_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.vs = vs;
    desc.fs = fs;
    return desc;
}

nw::gfx::PipelineDesc make_pbr_static_pipeline_desc(
    nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    nw::gfx::PipelineDesc desc{};
    desc.vs = vs;
    desc.fs = fs;
    desc.uses_bindless_sampled_textures = true;
    desc.uses_draw_uniforms = true;
    desc.uses_draw_uniforms2 = true;
    desc.uses_storage_buffer = true;
    desc.storage_buffer_count = 5;
    desc.uses_single_texture = false;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.vertex_stride = sizeof(nw::render::Vertex);
    desc.vertex_attributes = {
        {0, offsetof(nw::render::Vertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(nw::render::Vertex, normal), nw::gfx::VertexFormat::Float3},
        {2, offsetof(nw::render::Vertex, texcoord), nw::gfx::VertexFormat::Float2},
        {3, offsetof(nw::render::Vertex, tangent), nw::gfx::VertexFormat::Float4},
    };
    return desc;
}

nw::gfx::PipelineDesc make_pbr_skinned_pipeline_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs)
{
    auto desc = base;
    desc.vs = vs;
    desc.vertex_stride = sizeof(nw::render::SkinnedVertex);
    desc.vertex_attributes = {
        {0, offsetof(nw::render::SkinnedVertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(nw::render::SkinnedVertex, normal), nw::gfx::VertexFormat::Float3},
        {2, offsetof(nw::render::SkinnedVertex, texcoord), nw::gfx::VertexFormat::Float2},
        {3, offsetof(nw::render::SkinnedVertex, tangent), nw::gfx::VertexFormat::Float4},
        {4, offsetof(nw::render::SkinnedVertex, joint_indices), nw::gfx::VertexFormat::UByte4},
        {5, offsetof(nw::render::SkinnedVertex, joint_weights), nw::gfx::VertexFormat::UByte4Norm},
    };
    return desc;
}

nw::gfx::PipelineDesc make_pbr_shadow_pipeline_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.fs = fs;
    desc.has_color_attachment = false;
    desc.color_write = false;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.use_swapchain_color_format = false;
    desc.depth_attachment_format = nw::gfx::Fmt::D32F;
    return desc;
}

} // namespace nw::render
