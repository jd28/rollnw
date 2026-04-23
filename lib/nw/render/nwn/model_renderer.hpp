#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/render/render_context.hpp>
#include <nw/render/renderer.hpp>

#include <glm/glm.hpp>

namespace nw::render::nwn {

struct ModelInstance;
class ModelGpuBackend;
class RenderAssetCache;

struct ModelRenderContext {
    nw::gfx::Context* gfx = nullptr;
    ModelGpuBackend* gpu = nullptr;
    RenderAssetCache* assets = nullptr;
};

void render_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_model_instance_with_root(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const glm::mat4& model_root, const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_shadow_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const glm::mat4& light_view, const glm::mat4& light_projection);

} // namespace nw::render::nwn
