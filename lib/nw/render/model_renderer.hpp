#pragma once

#include <nw/render/model_draw.hpp>
#include <nw/render/model_render_context.hpp>
#include <nw/render/render_context.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <type_traits>

namespace nw::gfx {
struct CommandList;
}

namespace nw::render {

// Model draw protocol v1, b4 in both shader stages. Keep synchronized with
// model_draw_constants.inc.hlsl. SceneConstants at b1 is shared by the view;
// its legacy model/normal fields are unused by the common model shaders.
struct ModelDrawConstants {
    glm::mat4 model;
    glm::mat4 normal_matrix;
    SurfaceConstants surface;
};

static_assert(std::is_standard_layout_v<ModelDrawConstants>);
static_assert(offsetof(ModelDrawConstants, normal_matrix) == 64);
static_assert(offsetof(ModelDrawConstants, surface) == 128);
static_assert(sizeof(ModelDrawConstants) == 288);

// Upload immutable views into the current frame arena. Output length must equal
// input length. Invalid context/length clears outputs and returns false; failed
// allocations leave their output empty and return false. Successful spans live
// until this frame completes and must not be reused in a later frame or device.
[[nodiscard]] bool upload_render_model_views(const ModelRenderContext& render_ctx,
    std::span<const SceneConstants> views, std::span<nw::gfx::UniformSpan> outputs);
// Size-one wrappers over the batch upload. The RenderContext overload includes
// preview lighting and assigned shadow textures. An allocation failure returns
// a context with an empty view span; subsequent draws also fail if exhausted.
[[nodiscard]] ModelRenderContext prepare_render_model_view(
    const ModelRenderContext& render_ctx, const SceneConstants& view);
[[nodiscard]] ModelRenderContext prepare_render_model_view(
    const ModelRenderContext& render_ctx, const RenderContext& view);

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
// invalid or out-of-range spans are rejected without a draw. primitive_world
// rows supply full surface world/normal transforms and reject deforming meshes;
// they do not apply the primitive's bind transform a second time.
// winding describes the complete world transforms of every row in the batch;
// callers must partition differing orientations. Unknown preserves both sides.
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
    PreparedRenderModelSurfaceSubmissionStats* stats = nullptr,
    PreparedModelSurfaceInstanceSpace space = PreparedModelSurfaceInstanceSpace::model_root,
    ModelTransformWinding winding = ModelTransformWinding::unknown);
// Submits one shadow-casting unskinned primitive for a contiguous batch of
// caller-owned root or complete world transforms, with the same row/lifetime
// contract as the main pass. Invalid, translucent, skinned, mismatched, or
// out-of-range input rejects without a draw; world rows also reject deformers.
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
    const ModelMaterialOverrideStore* material_overrides = nullptr,
    PreparedModelSurfaceInstanceSpace space = PreparedModelSurfaceInstanceSpace::model_root);
void render_prepared_render_model_shadow_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    uint32_t range_index, const glm::mat4& light_view, const glm::mat4& light_projection,
    const PreparedRenderModelSkinTable* skin_table = nullptr,
    const ModelMaterialOverrideStore* material_overrides = nullptr);

} // namespace nw::render
