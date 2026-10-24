#pragma once

#include <nw/render/model_draw.hpp>
#include <nw/render/model_render_context.hpp>
#include <nw/render/render_context.hpp>

#include <span>

namespace nw::render::viewer {

struct PreviewScene;
class AreaRenderFrame;
class AreaRenderScene;

// CPU-only preparation result. Indices refer to the input surface and scene
// model arrays; no resource ownership or per-frame pointers are retained.
// Reuse requires unchanged rows, model identity, material bindings, instance
// space and roots. AreaRenderScene generations and runtime-state publication
// supply that lifetime for retained complete-static batches.
struct PreparedSurfaceBatches {
    struct Run {
        size_t begin = 0u;
        size_t end = 0u;
        uint32_t instance_source_index = nw::render::kInvalidPreparedModelDrawIndex;
    };
    struct Group {
        uint32_t representative_run = 0u;
        nw::render::PreparedModelSurfaceInstanceSpace space
            = nw::render::PreparedModelSurfaceInstanceSpace::model_root;
        std::vector<uint32_t> run_indices;
        // Packed in run_indices order only for retained model-root groups.
        // Fresh preparation and primitive-world groups leave this empty.
        std::vector<nw::render::PreparedModelSurfaceInstance> root_instances;
    };
    std::vector<uint32_t> surface_indices;
    std::vector<Run> runs;
    std::vector<Group> groups;
    std::vector<uint32_t> fallback_runs;
    uint32_t dropped_invalid_surface_count = 0u;
};

struct PreviewPreparedModelDraws {
    struct Transform {
        glm::mat4 world;
        nw::render::Bounds local_bounds;
        glm::mat4 normal_matrix;
        nw::render::Bounds world_bounds;
    };

    nw::render::PreparedModelDrawList common;
    nw::render::PreparedModelDrawRangeList ranges;
    // Preserved by clear(); reuse compares all numeric inputs at each draw
    // position. No model identity, material state or resource lifetime is cached.
    std::vector<Transform> transform_cache;

    void clear()
    {
        common.clear();
        ranges.clear();
    }
};

struct PreviewPreparedModelDrawMaterialStats {
    uint32_t opaque_count = 0;
    uint32_t cutout_count = 0;
    uint32_t transparent_count = 0;
    uint32_t water_count = 0;
    uint32_t invalid_mode_count = 0;

    [[nodiscard]] uint32_t total() const noexcept
    {
        return opaque_count + cutout_count + transparent_count + water_count + invalid_mode_count;
    }
};

struct PreviewPreparedModelDrawValidation {
    nw::render::PreparedModelDrawStats expected_stats{};
    nw::render::PreparedModelDrawStats prepared_stats{};
    PreviewPreparedModelDrawMaterialStats expected_materials{};
    PreviewPreparedModelDrawMaterialStats prepared_materials{};
    uint32_t prepared_draw_count = 0;
    uint32_t instance_offset_count = 0;
    uint32_t protocol_mismatch_count = 0;

    [[nodiscard]] bool valid() const noexcept { return protocol_mismatch_count == 0; }
};

struct PreviewPreparedModelSurfaceSubmissionStats {
    nw::render::PreparedRenderModelSurfaceSubmissionStats render_model{};
};

// Appends one asset's flat primitive batch to an existing common draw list.
// The caller owns handle/visibility accounting and the terminal instance
// offset; malformed primitive rows are dropped and counted in out.stats.
void append_prepared_render_model_draws(
    nw::render::PreparedModelDrawList& out,
    nw::render::ModelInstanceHandle handle,
    const nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    const nw::render::ModelMaterialOverrideStore& material_overrides);

void collect_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles);
void collect_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene);
void collect_prepared_model_surface_draws(
    PreviewPreparedModelDraws& prepared,
    nw::render::PreparedModelSurfaceDrawList& surfaces,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles);
void collect_prepared_model_surface_draws(
    PreviewPreparedModelDraws& prepared,
    nw::render::PreparedModelSurfaceDrawList& surfaces,
    const PreviewScene& scene);
nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draws(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const nw::render::PreparedRenderModelSkinTable* skin_table = nullptr,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch = nullptr);
// Submits a caller-owned flat batch of indices into stable prepared surfaces.
// The index stream is pass ordered and may contain many instances of a shared
// RenderModel. Invalid indices and source rows are dropped and counted; input
// storage must outlive this call. This avoids rebuilding cached area surfaces
// into transient per-frame draw records.
nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draw_indices(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const uint32_t> surface_indices,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const AreaRenderFrame* visibility_filter,
    const nw::render::PreparedRenderModelSkinTable* skin_table = nullptr,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch = nullptr);
// Consumes a frame prepared from area_scene. Retains complete static opaque /
// cutout groups; partial visibility and other passes use fresh preparation.
nw::render::PreparedRenderModelSurfaceSubmissionStats render_area_model_surface_draws(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    AreaRenderScene& area_scene,
    const AreaRenderFrame& frame,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch = nullptr);
PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::ModelInstanceHandle> handles);
PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared);

} // namespace nw::render::viewer
