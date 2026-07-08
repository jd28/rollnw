#pragma once

#include <nw/render/model_draw.hpp>
#include <nw/render/nwn/model_render_context.hpp>
#include <nw/render/nwn/model_renderer.hpp>

#include <limits>
#include <span>
#include <vector>

namespace nw::render {
struct RenderService;
} // namespace nw::render

namespace nw::render::viewer {

struct PreviewScene;

struct PreviewPreparedModelDraws {
    nw::render::PreparedModelDrawList common;
    nw::render::PreparedModelDrawRangeList ranges;
    std::vector<nw::render::nwn::PreparedDrawItem> nwn_draws;

    void clear()
    {
        common.clear();
        ranges.clear();
        nwn_draws.clear();
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
    uint32_t nwn_sidecar_draw_count = 0;
    uint32_t instance_offset_count = 0;
    uint32_t protocol_mismatch_count = 0;

    [[nodiscard]] bool valid() const noexcept { return protocol_mismatch_count == 0; }
};

struct PreviewPreparedNwnLegacyDrawItemStats {
    uint32_t input_draw_count = 0;
    uint32_t selected_draw_count = 0;
    uint32_t non_nwn_draw_count = 0;
    uint32_t missing_sidecar_draw_count = 0;
    uint32_t invalid_sidecar_draw_count = 0;

    [[nodiscard]] uint32_t dropped_draw_count() const noexcept
    {
        const uint64_t total = static_cast<uint64_t>(non_nwn_draw_count)
            + static_cast<uint64_t>(missing_sidecar_draw_count)
            + static_cast<uint64_t>(invalid_sidecar_draw_count);
        return total > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(total);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return missing_sidecar_draw_count == 0 && invalid_sidecar_draw_count == 0;
    }
};

struct PreviewPreparedNwnLegacySurfacePacketList {
    std::vector<uint32_t> surface_indices;
    PreviewPreparedNwnLegacyDrawItemStats stats{};

    void clear()
    {
        surface_indices.clear();
        stats = {};
    }
};

struct PreviewPreparedModelSurfaceSubmissionStats {
    PreviewPreparedNwnLegacyDrawItemStats nwn_legacy{};
    nw::render::PreparedRenderModelSurfaceSubmissionStats render_model{};
};

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
// Bridge-phase adapter: common records select the draw; the NWN sidecar still
// owns the legacy prepared draw payload. Non-NWN, out-of-range, and null-mesh
// records are dropped and counted at this boundary.
PreviewPreparedNwnLegacyDrawItemStats collect_nwn_legacy_prepared_draw_items(
    std::vector<const nw::render::nwn::PreparedDrawItem*>& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelDraw> draws);
void collect_nwn_legacy_prepared_surface_packets(
    PreviewPreparedNwnLegacySurfacePacketList& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces);
PreviewPreparedNwnLegacyDrawItemStats collect_nwn_legacy_prepared_surface_draw_items(
    std::vector<const nw::render::nwn::PreparedDrawItem*>& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces);
PreviewPreparedNwnLegacyDrawItemStats collect_nwn_legacy_prepared_surface_draw_items(
    std::vector<const nw::render::nwn::PreparedDrawItem*>& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    nw::render::RenderPassSelection pass);
nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draws(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const nw::render::PreparedRenderModelSkinTable* skin_table = nullptr,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch = nullptr);
PreviewPreparedNwnLegacyDrawItemStats render_prepared_nwn_legacy_surface_draws(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    nw::render::nwn::PreparedDrawScratch& scratch,
    std::vector<const nw::render::nwn::PreparedDrawItem*>& nwn_draw_items);

PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::ModelInstanceHandle> handles);
PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared);

} // namespace nw::render::viewer
