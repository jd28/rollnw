#pragma once

#include <nw/gfx/gfx.hpp>

namespace nw::render {

[[nodiscard]] nw::gfx::PipelineDesc make_offscreen_pipeline_desc(const nw::gfx::PipelineDesc& base);
[[nodiscard]] nw::gfx::PipelineDesc make_transparent_pipeline_desc(const nw::gfx::PipelineDesc& base);
[[nodiscard]] nw::gfx::PipelineDesc make_shader_variant_pipeline_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs);
[[nodiscard]] nw::gfx::PipelineDesc make_pbr_static_pipeline_desc(
    nw::gfx::Handle<nw::gfx::Shader> vs, nw::gfx::Handle<nw::gfx::Shader> fs);
[[nodiscard]] nw::gfx::PipelineDesc make_pbr_skinned_pipeline_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> vs);
[[nodiscard]] nw::gfx::PipelineDesc make_pbr_shadow_pipeline_desc(
    const nw::gfx::PipelineDesc& base, nw::gfx::Handle<nw::gfx::Shader> fs);

} // namespace nw::render
