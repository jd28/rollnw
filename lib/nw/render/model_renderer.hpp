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

// Common RenderModel renderer API. Source formats are lowered before this
// boundary; submission consumes only source-neutral model and frame records.
// Submits one immutable model for a batch of transient root transforms. This is
// used by mesh particles, whose instance state already lives in particle SoA
// storage and therefore does not belong in the persistent ModelInstanceStore.
// The caller owns the transform span for the duration of this call.
void render_render_model_instances(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const glm::mat4> model_roots,
    const RenderContext& ctx, RenderPassSelection pass = RenderPassSelection::all);
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
    PreparedRenderModelSurfaceSubmissionStats* stats = nullptr,
    PreparedRenderModelSurfacePacketList* packet_scratch = nullptr);
void render_prepared_render_model_shadow_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    uint32_t range_index, const glm::mat4& light_view, const glm::mat4& light_projection,
    const PreparedRenderModelSkinTable* skin_table = nullptr,
    const ModelMaterialOverrideStore* material_overrides = nullptr);

} // namespace nw::render
