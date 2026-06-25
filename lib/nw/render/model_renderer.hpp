#pragma once

#include <nw/render/model_draw.hpp>
#include <nw/render/model_render_context.hpp>
#include <nw/render/render_context.hpp>

#include <glm/glm.hpp>

#include <span>

namespace nw::gfx {
struct CommandList;
}

namespace nw::render {

// Bridge-phase common RenderModel renderer API. Modern RenderModel/PBR call
// sites should use this namespace and nw::render::ModelRenderContext directly;
// NWN sidecar renderers keep their legacy context for source-specific payloads.
void render_render_model_with_root(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, const glm::mat4& model_root, const RenderContext& ctx,
    RenderPassSelection pass = RenderPassSelection::all,
    const ModelInstance* instance = nullptr);
void collect_prepared_render_model_surface_packets(
    PreparedRenderModelSurfacePacketList& out,
    const RenderModel& model,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    RenderPassSelection pass = RenderPassSelection::all,
    const PreparedRenderModelSkinTable* skin_table = nullptr,
    const ModelMaterialOverrideStore* material_overrides = nullptr);
void render_prepared_render_model_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    const RenderContext& ctx,
    RenderPassSelection pass = RenderPassSelection::all,
    const PreparedRenderModelSkinTable* skin_table = nullptr,
    const ModelMaterialOverrideStore* material_overrides = nullptr,
    PreparedRenderModelSurfaceSubmissionStats* stats = nullptr);
void render_prepared_render_model_shadow_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    uint32_t range_index, const glm::mat4& light_view, const glm::mat4& light_projection,
    const PreparedRenderModelSkinTable* skin_table = nullptr,
    const ModelMaterialOverrideStore* material_overrides = nullptr);

} // namespace nw::render
