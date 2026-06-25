#include <nw/render/nwn/model_gpu_backend.hpp>

#include <nw/gfx/gfx.hpp>
#include <nw/log.hpp>
#include <nw/render/model_pipeline_desc.hpp>
#include <nw/render/nwn/model_loader.hpp>

#include <array>
#include <cstddef>

namespace nw::render {

namespace {

nw::gfx::PipelineDesc make_static_mesh_desc(
    nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    nw::gfx::PipelineDesc desc{};
    desc.vs = vs;
    desc.fs = fs;
    desc.uses_bindless_sampled_textures = true;
    desc.uses_draw_uniforms2 = true;
    desc.uses_storage_buffer = true;
    desc.storage_buffer_count = 5;
    desc.uses_single_texture = false;
    desc.depth_test = true;
    desc.depth_write = true;
    // LESS_EQUAL (not LESS) so opaque color draws survive a same-geometry depth pre-pass,
    // which writes their exact depth. Equivalent to LESS when no pre-pass runs.
    desc.depth_compare = nw::gfx::CompareOp::LessEqual;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.vertex_stride = sizeof(nwn::Vertex);
    desc.vertex_attributes = {
        {0, offsetof(nwn::Vertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(nwn::Vertex, normal), nw::gfx::VertexFormat::Float3},
        {2, offsetof(nwn::Vertex, texcoord), nw::gfx::VertexFormat::Float2},
        {3, offsetof(nwn::Vertex, tangent), nw::gfx::VertexFormat::Float4},
    };
    return desc;
}

nw::gfx::PipelineDesc make_static_shadow_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.vs = vs;
    desc.fs = fs;
    desc.uses_draw_uniforms2 = false;
    desc.uses_storage_buffer = false;
    desc.storage_buffer_count = 0;
    desc.has_color_attachment = false;
    desc.color_write = false;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.use_swapchain_color_format = false;
    desc.depth_attachment_format = nw::gfx::Fmt::D32F;
    return desc;
}

nw::gfx::PipelineDesc make_cutout_shadow_desc(const nw::gfx::PipelineDesc& base)
{
    auto desc = base;
    desc.uses_bindless_sampled_textures = true;
    desc.uses_single_texture = false;
    return desc;
}

nw::gfx::PipelineDesc make_static_batched_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.vs = vs;
    desc.fs = fs;
    desc.storage_buffer_count = 5;
    return desc;
}

nw::gfx::PipelineDesc make_static_batched_depth_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.fs = fs;
    desc.color_write = false;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.depth_compare = nw::gfx::CompareOp::Less;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    return desc;
}

nw::gfx::PipelineDesc make_static_batched_shadow_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.vs = vs;
    desc.fs = fs;
    desc.uses_storage_buffer = true;
    desc.storage_buffer_count = 2;
    return desc;
}

nw::gfx::PipelineDesc make_skinned_mesh_desc(
    nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    nw::gfx::PipelineDesc desc{};
    desc.vs = vs;
    desc.fs = fs;
    desc.uses_bindless_sampled_textures = true;
    desc.uses_draw_uniforms2 = true;
    desc.uses_storage_buffer = true;
    desc.storage_buffer_count = 5;
    desc.uses_single_texture = false;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.vertex_stride = sizeof(nwn::SkinnedVertex);
    desc.vertex_attributes = {
        {0, offsetof(nwn::SkinnedVertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(nwn::SkinnedVertex, normal), nw::gfx::VertexFormat::Float3},
        {2, offsetof(nwn::SkinnedVertex, texcoord), nw::gfx::VertexFormat::Float2},
        {3, offsetof(nwn::SkinnedVertex, tangent), nw::gfx::VertexFormat::Float4},
        {4, offsetof(nwn::SkinnedVertex, joint_indices), nw::gfx::VertexFormat::UByte4},
        {5, offsetof(nwn::SkinnedVertex, joint_weights), nw::gfx::VertexFormat::UByte4Norm},
    };
    return desc;
}

nw::gfx::PipelineDesc make_skinned_shadow_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs)
{
    auto desc = base;
    desc.vs = vs;
    desc.fs = fs;
    desc.uses_draw_uniforms2 = false;
    desc.storage_buffer_count = 2;
    desc.has_color_attachment = false;
    desc.color_write = false;
    desc.blend_mode = nw::gfx::BlendMode::disabled;
    desc.use_swapchain_color_format = false;
    desc.depth_attachment_format = nw::gfx::Fmt::D32F;
    return desc;
}

} // namespace

namespace nwn {

ModelGpuResources::~ModelGpuResources()
{
    destroy();
}

bool ModelGpuResources::initialize()
{
    return true;
}

void ModelGpuResources::destroy()
{
}

} // namespace nwn

bool ModelGpuBackend::initialize_nwn_legacy_resources(nw::render::ShaderProvider& shader_provider)
{
    auto vs_static = shader_provider.get_shader("render_static_mesh.vs.hlsl");
    auto vs_static_batched = shader_provider.get_shader("render_static_mesh_batched.vs.hlsl");
    auto vs_water = shader_provider.get_shader("render_water.vs.hlsl");
    auto vs_static_shadow = shader_provider.get_shader("render_shadow_static.vs.hlsl");
    auto vs_static_shadow_batched = shader_provider.get_shader("render_shadow_static_batched.vs.hlsl");
    auto ps_shadow = shader_provider.get_shader("render_shadow.ps.hlsl");
    auto ps_shadow_batched = shader_provider.get_shader("render_shadow_batched.ps.hlsl");
    auto ps = shader_provider.get_shader("render_pbr.ps.hlsl");
    auto ps_batched = shader_provider.get_shader("render_pbr_batched.ps.hlsl");
    auto ps_depth_only = shader_provider.get_shader("render_depth_only.ps.hlsl");
    auto ps_water = shader_provider.get_shader("render_water.ps.hlsl");
    if (!vs_static.valid() || !ps.valid() || !vs_static_shadow.valid() || !ps_shadow.valid()) {
        LOG_F(ERROR, "Failed to load static shaders");
        return false;
    }
    if (!vs_water.valid() || !ps_water.valid()) {
        LOG_F(WARNING, "Failed to load water shaders; water meshes will use transparent mesh shading");
    }
    if (!vs_static_batched.valid() || !ps_batched.valid()) {
        LOG_F(WARNING, "Failed to load batched static shaders; prepared static draws will use legacy binding");
    }
    if (!vs_static_shadow_batched.valid() || !ps_shadow_batched.valid()) {
        LOG_F(WARNING, "Failed to load batched static shadow shaders; area shadows will use legacy binding");
    }

    auto static_desc = make_static_mesh_desc(vs_static, ps);
    if (!require_pipeline_slot(PipelineSlot::static_mesh, static_desc, "Failed to create static mesh pipeline")) {
        return false;
    }

    auto static_shadow_desc = make_static_shadow_desc(static_desc, vs_static_shadow, ps_shadow);
    if (!require_pipeline_slot(PipelineSlot::static_shadow, static_shadow_desc, "Failed to create static shadow pipeline")) {
        return false;
    }

    auto static_shadow_cutout_desc = make_cutout_shadow_desc(static_shadow_desc);
    if (!require_pipeline_slot(PipelineSlot::static_shadow_cutout, static_shadow_cutout_desc, "Failed to create static cutout shadow pipeline")) {
        return false;
    }

    if (vs_static_shadow_batched.valid() && ps_shadow_batched.valid()) {
        auto static_batched_shadow_desc = make_static_batched_shadow_desc(
            static_shadow_cutout_desc, vs_static_shadow_batched, ps_shadow_batched);
        warn_pipeline_slot(PipelineSlot::static_batched_shadow, static_batched_shadow_desc, "Failed to create batched static shadow pipeline");
    }

    auto static_offscreen_desc = nw::render::make_offscreen_pipeline_desc(static_desc);
    if (!require_pipeline_slot(PipelineSlot::static_offscreen, static_offscreen_desc, "Failed to create offscreen static mesh pipeline")) {
        return false;
    }

    auto static_offscreen_transparent_desc = nw::render::make_transparent_pipeline_desc(static_offscreen_desc);
    if (!require_pipeline_slot(PipelineSlot::static_offscreen_transparent, static_offscreen_transparent_desc,
            "Failed to create offscreen transparent static mesh pipeline")) {
        return false;
    }

    auto static_transparent_desc = nw::render::make_transparent_pipeline_desc(static_desc);
    if (!require_pipeline_slot(PipelineSlot::static_transparent, static_transparent_desc, "Failed to create transparent static mesh pipeline")) {
        return false;
    }

    if (vs_static_batched.valid() && ps_batched.valid()) {
        auto static_batched_desc = make_static_batched_desc(static_desc, vs_static_batched, ps_batched);
        warn_pipeline_slot(PipelineSlot::static_batched, static_batched_desc, "Failed to create batched static mesh pipeline");

        if (ps_depth_only.valid()) {
            // Depth-only variant of the batched static pipeline: same vertex shader and
            // resource layout (so callers bind identical resources), color writes off,
            // LESS so it populates depth from a cleared buffer for the pre-pass.
            auto static_batched_depth_desc = make_static_batched_depth_desc(static_batched_desc, ps_depth_only);
            warn_pipeline_slot(PipelineSlot::static_batched_depth, static_batched_depth_desc,
                "Failed to create batched static depth pre-pass pipeline");
        }

        auto static_batched_offscreen_desc = make_static_batched_desc(static_offscreen_desc, vs_static_batched, ps_batched);
        warn_pipeline_slot(PipelineSlot::static_batched_offscreen, static_batched_offscreen_desc,
            "Failed to create offscreen batched static mesh pipeline");

        auto static_batched_transparent_desc = make_static_batched_desc(static_transparent_desc, vs_static_batched, ps_batched);
        warn_pipeline_slot(PipelineSlot::static_batched_transparent, static_batched_transparent_desc,
            "Failed to create batched transparent static mesh pipeline");

        auto static_batched_offscreen_transparent_desc = make_static_batched_desc(
            static_offscreen_transparent_desc, vs_static_batched, ps_batched);
        warn_pipeline_slot(PipelineSlot::static_batched_offscreen_transparent, static_batched_offscreen_transparent_desc,
            "Failed to create offscreen batched transparent static mesh pipeline");
    }

    if (vs_water.valid() && ps_water.valid()) {
        auto static_water_desc = nw::render::make_shader_variant_pipeline_desc(static_transparent_desc, vs_water, ps_water);
        warn_pipeline_slot(PipelineSlot::static_water, static_water_desc, "Failed to create water static mesh pipeline");

        auto static_offscreen_water_desc = nw::render::make_shader_variant_pipeline_desc(static_offscreen_transparent_desc, vs_water, ps_water);
        warn_pipeline_slot(PipelineSlot::static_offscreen_water, static_offscreen_water_desc,
            "Failed to create offscreen water static mesh pipeline");
    }

    auto vs_skinned = shader_provider.get_shader("render_skinned_mesh.vs.hlsl");
    auto vs_skinned_shadow = shader_provider.get_shader("render_shadow_skinned.vs.hlsl");
    if (vs_skinned.valid()) {
        auto skin_desc = make_skinned_mesh_desc(vs_skinned, ps);
        if (warn_pipeline_slot(PipelineSlot::skinned, skin_desc, "Failed to create skinned mesh pipeline — skinned meshes will use static pipeline")) {
            if (vs_skinned_shadow.valid()) {
                auto skin_shadow_desc = make_skinned_shadow_desc(skin_desc, vs_skinned_shadow, ps_shadow);
                warn_pipeline_slot(PipelineSlot::skinned_shadow, skin_shadow_desc, "Failed to create skinned shadow pipeline");

                auto skin_shadow_cutout_desc = make_cutout_shadow_desc(skin_shadow_desc);
                warn_pipeline_slot(PipelineSlot::skinned_shadow_cutout, skin_shadow_cutout_desc,
                    "Failed to create skinned cutout shadow pipeline");
            }

            auto skin_transparent_desc = nw::render::make_transparent_pipeline_desc(skin_desc);
            warn_pipeline_slot(PipelineSlot::skinned_transparent, skin_transparent_desc, "Failed to create transparent skinned mesh pipeline");

            auto skin_offscreen_desc = nw::render::make_offscreen_pipeline_desc(skin_desc);
            warn_pipeline_slot(PipelineSlot::skinned_offscreen, skin_offscreen_desc, "Failed to create offscreen skinned mesh pipeline");

            auto skin_offscreen_transparent_desc = nw::render::make_transparent_pipeline_desc(skin_offscreen_desc);
            warn_pipeline_slot(PipelineSlot::skinned_offscreen_transparent, skin_offscreen_transparent_desc,
                "Failed to create offscreen transparent skinned mesh pipeline");
        }
    }

    return true;
}

bool ModelGpuBackend::initialize(nw::render::ShaderProvider& shader_provider)
{
    // Bridge split: the backend still builds NWN vertex-layout pipelines here,
    // while common storage owns resources shared by legacy and RenderModel PBR
    // shaders. Startup remains split into common storage, NWN legacy pipelines,
    // RenderModel PBR, and shared fallback phases.
    if (!initialize_common_storage_resources()) {
        return false;
    }

    if (!initialize_nwn_legacy_resources(shader_provider)) {
        return false;
    }

    if (!initialize_render_model_pbr_resources(shader_provider)) {
        return false;
    }

    initialize_shared_fallback_textures();

    return true;
}

} // namespace nw::render
