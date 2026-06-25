#pragma once

#include "area_render_scene.hpp"
#include "preview_scene.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/render/model_render_context.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace nw::render {
struct PreparedModelSurfaceDrawList;
struct RenderService;
} // namespace nw::render

namespace nw::render::nwn {
struct PreparedDrawScratch;
} // namespace nw::render::nwn

namespace nw::render::viewer {

class AreaRenderFrame;
struct PreviewPreparedModelDraws;

inline constexpr uint32_t kSceneShadowMapResolution = 1024;

struct ShadowRenderStats {
    uint32_t cascade_count = 0;
    uint32_t caster_model_count = 0;
    uint32_t no_caster_model_count = 0;
    uint32_t submitted_model_count = 0;
    uint32_t culled_model_count = 0;
    uint32_t prepared_surface_shadow_range_count = 0;
    uint32_t prepared_surface_invalid_range_count = 0;
    AreaPreparedSurfaceSidecarStats area_indirect_sidecar_bridge{};
    AreaPreparedSurfaceSidecarStats area_sidecar_bridge{};
};

[[nodiscard]] uint32_t viewer_shadow_map_resolution();
[[nodiscard]] uint32_t viewer_shadow_cascade_count();

[[nodiscard]] SceneShadow resolve_scene_shadow(
    const RenderContext& ctx,
    const Bounds& bounds,
    uint32_t shadow_map_resolution = kSceneShadowMapResolution,
    uint32_t active_cascade_count = kShadowCascadeCount);

bool render_scene_shadow_maps(
    nw::render::RenderService& render_service,
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    SceneShadow& shadow,
    uint32_t shadow_map_resolution = kSceneShadowMapResolution,
    ShadowRenderStats* stats = nullptr,
    AreaRenderFrame* area_frame = nullptr,
    nw::render::nwn::PreparedDrawScratch* scratch = nullptr,
    const PreviewPreparedModelDraws* prepared_model_draws = nullptr,
    const nw::render::PreparedModelSurfaceDrawList* prepared_model_surfaces = nullptr);

inline constexpr uint32_t kLocalShadowMapResolution = 1024;

struct LocalShadowRenderStats {
    uint32_t caster_light_count = 0; // shadow-casting lights selected (= slots used)
    uint32_t submitted_model_count = 0;
    uint32_t culled_model_count = 0;
    AreaPreparedSurfaceSidecarStats area_sidecar_bridge{};
};

// Select the top-K shadow-casting local lights, assign each a shadow slot
// (writing LocalLight::shadow_slot in `lights`), and compute its top-down ortho
// world->shadow matrix. K is a cost cap (kLocalShadowCount), not an aesthetic one.
[[nodiscard]] SceneLocalShadows resolve_local_shadows(
    const RenderContext& ctx,
    std::span<LocalLight> lights,
    std::optional<std::span<const uint32_t>> candidate_light_indices = std::nullopt);

// Render one depth-only top-down ortho map per selected caster into the local
// shadow atlas slots and populate `local_shadows.depth_textures`.
bool render_local_shadow_maps(
    nw::render::RenderService& render_service,
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    SceneLocalShadows& local_shadows,
    uint32_t shadow_map_resolution = kLocalShadowMapResolution,
    LocalShadowRenderStats* stats = nullptr,
    nw::render::nwn::PreparedDrawScratch* scratch = nullptr,
    AreaRenderFrame* area_frame = nullptr,
    const PreviewPreparedModelDraws* prepared_model_draws = nullptr,
    const nw::render::PreparedModelSurfaceDrawList* prepared_model_surfaces = nullptr);

} // namespace nw::render::viewer
