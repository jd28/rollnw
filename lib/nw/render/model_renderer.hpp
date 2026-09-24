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
// Submits a batch of unskinned surface poses for one validated model primitive
// and material. prototype supplies the immutable primitive/material payload;
// instances is caller-owned transient frame data. Empty, translucent, skinned,
// out-of-range, or mismatched input is rejected without a draw.
void render_prepared_render_model_surface_instances(
    const ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    const RenderModel& model,
    const PreparedModelSurfaceDraw& prototype,
    std::span<const PreparedModelSurfaceInstance> instances,
    const RenderContext& ctx,
    RenderPassSelection pass,
    const ModelMaterialOverrideStore* material_overrides = nullptr,
    PreparedRenderModelSurfaceSubmissionStats* stats = nullptr);
// Same batch protocol with instance rows already stored in one caller-owned
// frame allocation. first_instance selects this primitive's contiguous rows;
// invalid or out-of-range spans are rejected without a draw.
void render_prepared_render_model_surface_instances(
    const ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    const RenderModel& model,
    const PreparedModelSurfaceDraw& prototype,
    nw::gfx::StorageSpan instance_storage,
    uint32_t first_instance,
    uint32_t instance_count,
    const RenderContext& ctx,
    RenderPassSelection pass,
    const ModelMaterialOverrideStore* material_overrides = nullptr,
    PreparedRenderModelSurfaceSubmissionStats* stats = nullptr);
// Submits one shadow-casting unskinned primitive for a contiguous batch of
// caller-owned root transforms. Invalid, translucent, skinned, mismatched, or
// out-of-range input is rejected without recording a draw.
[[nodiscard]] bool render_prepared_render_model_shadow_surface_instances(
    const ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    const RenderModel& model,
    const PreparedModelSurfaceDraw& prototype,
    nw::gfx::StorageSpan instance_storage,
    uint32_t first_instance,
    uint32_t instance_count,
    const glm::mat4& light_view,
    const glm::mat4& light_projection,
    const ModelMaterialOverrideStore* material_overrides = nullptr);
void render_prepared_render_model_shadow_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    uint32_t range_index, const glm::mat4& light_view, const glm::mat4& light_projection,
    const PreparedRenderModelSkinTable* skin_table = nullptr,
    const ModelMaterialOverrideStore* material_overrides = nullptr);

} // namespace nw::render
