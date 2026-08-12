#pragma once

#include <nw/render/model_draw.hpp>
#include <nw/render/model_render_context.hpp>
#include <nw/render/render_context.hpp>

#include <span>

namespace nw::render::viewer {

struct PreviewScene;

struct PreviewPreparedModelDraws {
    nw::render::PreparedModelDrawList common;
    nw::render::PreparedModelDrawRangeList ranges;

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
PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::ModelInstanceHandle> handles);
PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared);

} // namespace nw::render::viewer
