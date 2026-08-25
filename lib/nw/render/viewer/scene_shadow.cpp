#include "scene_shadow.hpp"

#include "preview_model_animation.hpp"
#include "preview_model_draws.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/render/local_shadow_renderer.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/shadow_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace nw::render::viewer {

namespace {

static constexpr float kShadowMaxDistance = 120.0f;
static constexpr float kShadowCascadeBlendDistance = 8.0f;
static constexpr float kShadowCascadeBlendFraction = 0.12f;
static constexpr uint32_t kMinShadowMapResolution = 256;
static constexpr uint32_t kMaxShadowMapResolution = 4096;
static constexpr uint32_t kMinShadowCascadeCount = 1;

std::array<glm::vec3, 8> bounds_corners_world(const Bounds& bounds)
{
    std::array<glm::vec3, 8> corners{};
    size_t index = 0;
    for (float z : {bounds.min.z, bounds.max.z}) {
        for (float y : {bounds.min.y, bounds.max.y}) {
            for (float x : {bounds.min.x, bounds.max.x}) {
                corners[index++] = glm::vec3{x, y, z};
            }
        }
    }
    return corners;
}

bool bounds_intersects_shadow_clip(const Bounds& bounds, const glm::mat4& world_to_shadow)
{
    glm::vec3 min_clip{std::numeric_limits<float>::max()};
    glm::vec3 max_clip{std::numeric_limits<float>::lowest()};
    for (const auto& corner : bounds_corners_world(bounds)) {
        const glm::vec4 clip = world_to_shadow * glm::vec4(corner, 1.0f);
        if (std::abs(clip.w) <= 1.0e-6f) {
            continue;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        min_clip = glm::min(min_clip, ndc);
        max_clip = glm::max(max_clip, ndc);
    }

    constexpr float kClipPadding = 0.05f;
    return max_clip.x >= -1.0f - kClipPadding
        && min_clip.x <= 1.0f + kClipPadding
        && max_clip.y >= -1.0f - kClipPadding
        && min_clip.y <= 1.0f + kClipPadding
        && max_clip.z >= 0.0f - kClipPadding
        && min_clip.z <= 1.0f + kClipPadding;
}

float shadow_cascade_blend_width(float previous_split_distance, float split_distance) noexcept
{
    const float span = std::max(split_distance - previous_split_distance, 1.0f);
    return std::min(kShadowCascadeBlendDistance, span * kShadowCascadeBlendFraction);
}

struct ShadowRenderModelCandidate {
    uint32_t model_index = 0;
    uint32_t prepared_draw_range_index = std::numeric_limits<uint32_t>::max();
    Bounds bounds{};
};

uint32_t saturating_count(size_t value)
{
    return static_cast<uint32_t>(std::min<size_t>(value, std::numeric_limits<uint32_t>::max()));
}

bool prepared_shadow_range_casts_shadow(std::span<const uint8_t> range_casts_shadow, uint32_t range_index) noexcept
{
    return range_index < range_casts_shadow.size()
        && range_casts_shadow[range_index] != 0u;
}

template <typename Candidate>
void assign_shadow_prepared_ranges(
    std::vector<Candidate>& candidates,
    std::span<const uint32_t> candidate_by_source,
    const nw::render::PreparedModelDrawRangeList& ranges)
{
    for (size_t range_index = 0; range_index < ranges.ranges.size(); ++range_index) {
        const auto& range = ranges.ranges[range_index];
        if (range.instance_source_index >= candidate_by_source.size()
            || range_index > std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const uint32_t candidate_index = candidate_by_source[range.instance_source_index];
        if (candidate_index < candidates.size()) {
            candidates[candidate_index].prepared_draw_range_index = static_cast<uint32_t>(range_index);
        }
    }
}

template <typename Candidate>
uint32_t drop_non_shadow_prepared_ranges(
    std::vector<Candidate>& candidates,
    std::span<const uint8_t> range_casts_shadow)
{
    uint32_t dropped_count = 0;
    auto removed_begin = std::remove_if(
        candidates.begin(),
        candidates.end(),
        [&](const auto& candidate) {
            if (prepared_shadow_range_casts_shadow(range_casts_shadow, candidate.prepared_draw_range_index)) {
                return false;
            }
            ++dropped_count;
            return true;
        });
    candidates.erase(removed_begin, candidates.end());
    return dropped_count;
}

bool area_record_has_flag(uint8_t flags, AreaRenderScene::RecordFlag flag) noexcept
{
    return (flags & static_cast<uint8_t>(flag)) != 0;
}

bool shadow_pipeline_ready(
    nw::render::ShadowRenderer& shadow_renderer,
    const nw::render::ModelRenderContext& model_render_ctx,
    uint32_t shadow_map_resolution)
{
    const auto shadow_pipeline = model_render_ctx.gpu
        ? model_render_ctx.gpu->pipeline({
              .mesh = nw::render::ModelPipelineMeshKind::pbr_static,
              .material = nw::render::MaterialMode::opaque,
              .pass = nw::render::ModelPipelinePass::shadow,
          })
        : nw::gfx::Handle<nw::gfx::Pipeline>{};
    return shadow_pipeline.valid()
        && shadow_renderer.ensure_resources(shadow_map_resolution);
}

Bounds area_shadow_record_bounds(const AreaRenderScene& area_scene, uint32_t record_index)
{
    const auto bounds = area_scene.bounds();
    if (record_index < bounds.size()) {
        return bounds[record_index];
    }
    return {};
}

// Batch transform from area record columns to RenderModel shadow candidates.
// Stale handles and out-of-range source indices are dropped before prepared
// range matching or Vulkan submission.
void collect_area_shadow_render_model_candidates(
    std::vector<ShadowRenderModelCandidate>& out,
    std::vector<uint32_t>& candidate_by_source,
    std::vector<nw::render::ModelInstanceHandle>* handles,
    const PreviewScene& scene,
    const AreaRenderScene& area_scene,
    std::span<const uint32_t> record_indices)
{
    out.clear();
    candidate_by_source.assign(scene.static_models.size(), std::numeric_limits<uint32_t>::max());
    out.reserve(record_indices.size());
    if (handles) {
        handles->clear();
        handles->reserve(record_indices.size());
    }

    const auto model_indices = area_scene.model_indices();
    const auto model_instance_handles = area_scene.model_instance_handles();

    for (const uint32_t record_index : record_indices) {
        if (record_index >= model_indices.size()
            || record_index >= model_instance_handles.size()) {
            continue;
        }
        const uint32_t model_index = model_indices[record_index];
        const auto instance_handle = model_instance_handles[record_index];
        const auto* instance = scene.model_instances.get(instance_handle);
        if (model_index >= scene.static_models.size()
            || !scene.static_models[model_index]
            || !instance
            || instance->render_model_index != model_index
            || !instance->visible
            || out.size() >= std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        out.push_back(ShadowRenderModelCandidate{
            .model_index = model_index,
            .bounds = area_shadow_record_bounds(area_scene, record_index),
        });
        candidate_by_source[model_index] = static_cast<uint32_t>(out.size() - 1u);
        if (handles) {
            handles->push_back(instance_handle);
        }
    }
}

std::array<glm::vec3, 8> frustum_corners_world(const glm::mat4& inv_view_projection)
{
    std::array<glm::vec3, 8> corners{};
    size_t index = 0;
    for (float z : {0.0f, 1.0f}) {
        for (float y : {-1.0f, 1.0f}) {
            for (float x : {-1.0f, 1.0f}) {
                const glm::vec4 corner = inv_view_projection * glm::vec4{x, y, z, 1.0f};
                corners[index++] = glm::vec3{corner} / corner.w;
            }
        }
    }
    return corners;
}

std::array<glm::vec3, 8> slice_frustum_corners(
    const std::array<glm::vec3, 8>& frustum_corners,
    float near_ratio,
    float far_ratio)
{
    std::array<glm::vec3, 8> slice{};
    for (size_t i = 0; i < 4; ++i) {
        const glm::vec3 near_corner = frustum_corners[i];
        const glm::vec3 far_corner = frustum_corners[i + 4];
        slice[i] = near_corner + (far_corner - near_corner) * near_ratio;
        slice[i + 4] = near_corner + (far_corner - near_corner) * far_ratio;
    }
    return slice;
}

glm::mat4 fit_shadow_matrix(
    const std::array<glm::vec3, 8>& corners,
    const glm::vec3& light_dir,
    float radius_scale,
    uint32_t shadow_map_resolution)
{
    glm::vec3 center{0.0f};
    for (const auto& corner : corners) {
        center += corner;
    }
    center /= static_cast<float>(corners.size());

    const glm::vec3 eye = center + light_dir * radius_scale;
    const glm::mat4 light_view = glm::lookAtRH(eye, center, glm::vec3{0.0f, 0.0f, 1.0f});

    glm::vec3 mins{std::numeric_limits<float>::max()};
    glm::vec3 maxs{std::numeric_limits<float>::lowest()};
    for (const auto& corner : corners) {
        const glm::vec3 light_space = glm::vec3(light_view * glm::vec4{corner, 1.0f});
        mins = glm::min(mins, light_space);
        maxs = glm::max(maxs, light_space);
    }

    const float padding = 8.0f;
    mins.z -= padding;
    maxs.z += padding;

    glm::mat4 light_projection = glm::orthoRH_ZO(
        mins.x, maxs.x, mins.y, maxs.y, -maxs.z, -mins.z);

    // The light view follows the camera slice. Snapping camera-relative bounds
    // does not stabilize a stationary world point because the view translation
    // still changes continuously. Quantize the completed projection's world
    // origin instead, so camera translation advances the shadow map only in
    // whole texels.
    const float half_resolution = static_cast<float>(
                                      std::max(shadow_map_resolution, 1u))
        * 0.5f;
    const glm::mat4 world_to_shadow = light_projection * light_view;
    const glm::vec4 shadow_origin = world_to_shadow * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec2 texel_origin = glm::vec2{shadow_origin} * half_resolution;
    const glm::vec2 texel_offset = glm::round(texel_origin) - texel_origin;
    light_projection[3].x += texel_offset.x / half_resolution;
    light_projection[3].y += texel_offset.y / half_resolution;

    return light_projection * light_view;
}

uint32_t environment_uint(const char* name, uint32_t fallback, uint32_t min_value, uint32_t max_value)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value) {
        return fallback;
    }

    return std::clamp(static_cast<uint32_t>(parsed), min_value, max_value);
}

std::array<float, kShadowCascadeCount> split_ratios_for(uint32_t active_cascade_count, bool orthographic)
{
    if (active_cascade_count <= 1) {
        return {1.0f, 1.0f, 1.0f};
    }
    if (active_cascade_count == 2) {
        return orthographic
            ? std::array<float, kShadowCascadeCount>{0.42f, 1.0f, 1.0f}
            : std::array<float, kShadowCascadeCount>{0.24f, 1.0f, 1.0f};
    }
    return orthographic
        ? std::array<float, kShadowCascadeCount>{0.22f, 0.55f, 1.0f}
        : std::array<float, kShadowCascadeCount>{0.10f, 0.32f, 1.0f};
}

// Local-light shadow tuning. A single orthographic top-down map sized to the
// light's radius covers floor/character shadows within its footprint (design C4).
// Orthographic (rather than a perspective cone from the low light position) keeps
// the whole illuminated disk inside the frustum regardless of the light's height.
constexpr float kLocalShadowStrength = 0.85f;

float local_shadow_candidate_score(const LocalLight& light) noexcept
{
    float score = light.intensity * light.radius;
    if (!std::isfinite(score)) {
        return 0.0f;
    }

    if (light.casts_shadow) {
        score *= 4.0f; // authored shadow casters win ties over incidental lights
    }
    return std::isfinite(score) ? score : 0.0f;
}

glm::mat4 local_shadow_world_to_shadow(const LocalLight& light)
{
    const float radius = std::max(light.radius, 0.1f);
    // Look straight down from just above the light. The eye must sit at the light,
    // NOT high above the room: geometry above the light (ceilings, rafters) would
    // otherwise be captured and cast shadows the light itself never sees.
    constexpr float kLift = 0.25f;
    const glm::vec3 eye = light.position + glm::vec3{0.0f, 0.0f, kLift};
    const glm::mat4 view = glm::lookAtRH(eye, light.position, glm::vec3{0.0f, 1.0f, 0.0f});
    const float near_plane = 0.02f;
    const float far_plane = radius + kLift + 1.0f;
    const glm::mat4 proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, near_plane, far_plane);
    return proj * view;
}

} // namespace

uint32_t viewer_shadow_map_resolution()
{
    static const uint32_t resolution = environment_uint(
        "ROLLNW_VIEWER_SHADOW_RESOLUTION", kSceneShadowMapResolution, kMinShadowMapResolution, kMaxShadowMapResolution);
    return resolution;
}

uint32_t viewer_shadow_cascade_count()
{
    static const uint32_t cascade_count = environment_uint(
        "ROLLNW_VIEWER_SHADOW_CASCADES", kShadowCascadeCount, kMinShadowCascadeCount, kShadowCascadeCount);
    return cascade_count;
}

SceneShadow resolve_scene_shadow(
    const RenderContext& ctx, const Bounds& bounds, uint32_t shadow_map_resolution, uint32_t active_cascade_count)
{
    if (ctx.lighting_space != LightingSpace::world_space) {
        return {};
    }
    if (ctx.lighting.key_intensity <= 1.0e-4f) {
        return {};
    }

    const glm::vec3 light_dir = glm::normalize(ctx.lighting.key_direction);
    if (!std::isfinite(light_dir.x) || !std::isfinite(light_dir.y) || !std::isfinite(light_dir.z)
        || glm::dot(light_dir, light_dir) < 1.0e-6f) {
        return {};
    }

    SceneShadow result{};
    result.enabled = true;
    result.cascade_count = std::clamp(active_cascade_count, kMinShadowCascadeCount, kShadowCascadeCount);
    result.strength = std::clamp(0.35f + ctx.lighting.key_intensity * 0.23f, 0.35f, 0.85f);

    const glm::mat4 inv_view_projection = glm::inverse(ctx.projection * ctx.view);
    const auto frustum_corners = frustum_corners_world(inv_view_projection);
    const float shadow_distance = std::min(ctx.camera_far_plane,
        ctx.orthographic_camera ? kShadowMaxDistance * 1.5f : kShadowMaxDistance);
    const float shadow_ratio = std::clamp(shadow_distance / std::max(ctx.camera_far_plane, 1.0e-4f), 0.0f, 1.0f);
    const std::array<float, kShadowCascadeCount> split_ratios = split_ratios_for(result.cascade_count, ctx.orthographic_camera);
    const float camera_far_plane = std::max(ctx.camera_far_plane, 1.0e-4f);
    std::array<float, kShadowCascadeCount> split_distances{};
    for (size_t i = 0; i < result.cascade_count; ++i) {
        split_distances[i] = shadow_distance * split_ratios[i];
        result.split_distances[i] = split_distances[i];
    }

    const float caster_radius = std::max(bounds.radius() * 2.5f, 80.0f);
    for (size_t i = 0; i < result.cascade_count; ++i) {
        const float previous_split_distance = i == 0 ? 0.0f : split_distances[i - 1];
        const float previous_previous_split_distance = i <= 1 ? 0.0f : split_distances[i - 2];
        float near_distance = previous_split_distance;
        float far_distance = split_distances[i];
        if (i > 0) {
            near_distance = std::max(
                0.0f,
                near_distance - shadow_cascade_blend_width(previous_previous_split_distance, previous_split_distance));
        }
        if (i + 1 < result.cascade_count) {
            far_distance = std::min(
                shadow_distance,
                far_distance + shadow_cascade_blend_width(previous_split_distance, split_distances[i]));
        }

        const float near_ratio = std::clamp(near_distance / camera_far_plane, 0.0f, shadow_ratio);
        const float far_ratio = std::clamp(far_distance / camera_far_plane, near_ratio, shadow_ratio);
        const auto corners = slice_frustum_corners(frustum_corners, near_ratio, far_ratio);
        result.world_to_shadow[i] = fit_shadow_matrix(corners, light_dir, caster_radius, shadow_map_resolution);
    }
    for (size_t i = result.cascade_count; i < kShadowCascadeCount; ++i) {
        result.split_distances[i] = shadow_distance;
    }
    return result;
}

bool render_scene_shadow_maps(
    nw::render::RenderService& render_service,
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    SceneShadow& shadow,
    uint32_t shadow_map_resolution,
    ShadowRenderStats* stats,
    AreaRenderFrame* area_frame,
    const PreviewPreparedModelDraws* frame_prepared_model_draws,
    const nw::render::PreparedModelSurfaceDrawList* frame_prepared_model_surfaces)
{
    if (!cmd || !shadow.enabled) {
        return false;
    }

    auto& shadow_renderer = render_service.shadow_renderer();
    std::vector<ShadowRenderModelCandidate> shadow_render_models;
    std::vector<nw::render::ModelInstanceHandle> shadow_render_model_handles;
    std::vector<uint32_t> shadow_render_model_candidate_by_source;
    uint32_t no_caster_model_count = 0;
    if (scene.area_render_scene) {
        const auto& area_scene = *scene.area_render_scene;
        std::vector<uint32_t> local_shadow_caster_records;
        std::span<const uint32_t> shadow_caster_records;
        if (area_frame) {
            shadow_caster_records = area_frame->shadow_caster_record_indices();
            no_caster_model_count = area_frame->stats().visible_record_count
                    > shadow_caster_records.size()
                ? area_frame->stats().visible_record_count - saturating_count(shadow_caster_records.size())
                : 0u;
        } else {
            const auto flags = area_scene.flags();
            local_shadow_caster_records.reserve(flags.size());
            for (uint32_t record_index = 0; record_index < flags.size(); ++record_index) {
                if (!area_record_has_flag(flags[record_index], AreaRenderScene::RecordFlag::render_enabled)) {
                    continue;
                }
                if (area_record_has_flag(flags[record_index], AreaRenderScene::RecordFlag::shadow_caster)) {
                    local_shadow_caster_records.push_back(record_index);
                } else {
                    ++no_caster_model_count;
                }
            }
            shadow_caster_records = local_shadow_caster_records;
        }
        collect_area_shadow_render_model_candidates(
            shadow_render_models,
            shadow_render_model_candidate_by_source,
            &shadow_render_model_handles,
            scene,
            area_scene,
            shadow_caster_records);
    } else {
        shadow_render_models.reserve(scene.static_models.size());
        shadow_render_model_handles.reserve(scene.static_models.size());
        shadow_render_model_candidate_by_source.assign(
            scene.static_models.size(), std::numeric_limits<uint32_t>::max());
        for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
            if (model_index >= scene.static_model_instance_handles.size()
                || model_index > std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            const auto& model = scene.static_models[model_index];
            const auto* instance = scene.static_model_instance(model_index);
            if (!model || !instance || !instance->visible) {
                continue;
            }
            if (!instance->shadow.casts_shadow) {
                ++no_caster_model_count;
                continue;
            }

            shadow_render_models.push_back(ShadowRenderModelCandidate{
                .model_index = static_cast<uint32_t>(model_index),
                .bounds = instance->shadow.bounds,
            });
            shadow_render_model_candidate_by_source[model_index]
                = static_cast<uint32_t>(shadow_render_models.size() - 1u);
            shadow_render_model_handles.push_back(scene.static_model_instance_handles[model_index]);
        }
    }

    PreviewPreparedModelDraws local_shadow_draws;
    nw::render::PreparedModelSurfaceDrawList local_shadow_surfaces;
    std::vector<nw::render::ModelInstanceHandle> local_shadow_handles;
    const PreviewPreparedModelDraws* shadow_draws = frame_prepared_model_draws;
    const nw::render::PreparedModelSurfaceDrawList* shadow_surfaces = frame_prepared_model_surfaces;
    if (!shadow_draws || !shadow_surfaces) {
        local_shadow_handles.reserve(shadow_render_model_handles.size());
        local_shadow_handles.insert(
            local_shadow_handles.end(),
            shadow_render_model_handles.begin(),
            shadow_render_model_handles.end());
        if (!local_shadow_handles.empty()) {
            collect_prepared_model_draws(
                local_shadow_draws,
                scene,
                std::span<const nw::render::ModelInstanceHandle>{local_shadow_handles});
            nw::render::collect_prepared_model_surface_draws(
                local_shadow_surfaces,
                local_shadow_draws.common,
                local_shadow_draws.ranges,
                scene.model_instances);
        }
        shadow_draws = &local_shadow_draws;
        shadow_surfaces = &local_shadow_surfaces;
    }

    std::vector<uint8_t> shadow_range_casts_shadow;
    const auto shadow_range_stats = nw::render::collect_prepared_model_surface_shadow_ranges(
        shadow_range_casts_shadow,
        std::span<const nw::render::PreparedModelSurfaceDraw>{
            shadow_surfaces->draws.data(),
            shadow_surfaces->draws.size()},
        shadow_draws->ranges.ranges.size());
    const std::span<const nw::render::PreparedModelSurfaceDraw> shadow_surface_span{
        shadow_surfaces->draws.data(),
        shadow_surfaces->draws.size()};

    assign_shadow_prepared_ranges(
        shadow_render_models,
        shadow_render_model_candidate_by_source,
        shadow_draws->ranges);
    no_caster_model_count += drop_non_shadow_prepared_ranges(shadow_render_models, shadow_range_casts_shadow);

    ShadowRenderStats local_stats{};
    local_stats.prepared_surface_shadow_range_count = shadow_range_stats.shadow_range_count;
    local_stats.prepared_surface_invalid_range_count = shadow_range_stats.invalid_range_index_count;

    local_stats.caster_model_count = saturating_count(shadow_render_models.size());
    local_stats.no_caster_model_count = no_caster_model_count;
    for (uint32_t cascade = 0; cascade < shadow.cascade_count; ++cascade) {
        for (const auto& candidate : shadow_render_models) {
            if (!bounds_intersects_shadow_clip(candidate.bounds, shadow.world_to_shadow[cascade])) {
                ++local_stats.culled_model_count;
                continue;
            }
            ++local_stats.submitted_model_count;
        }
    }
    if (local_stats.submitted_model_count == 0) {
        shadow.enabled = false;
        if (stats) {
            *stats = local_stats;
        }
        return false;
    }

    if (!shadow_pipeline_ready(shadow_renderer, render_model_ctx, shadow_map_resolution)) {
        shadow.enabled = false;
        if (stats) {
            *stats = local_stats;
        }
        return false;
    }

    for (uint32_t cascade = 0; cascade < shadow.cascade_count; ++cascade) {
        ++local_stats.cascade_count;
        shadow.depth_textures[cascade] = shadow_renderer.depth_texture(cascade);
        nw::gfx::cmd_begin_render(cmd, shadow_renderer.render_target(cascade));
        nw::gfx::cmd_set_viewport(cmd, 0.0f, 0.0f,
            static_cast<float>(shadow_map_resolution),
            static_cast<float>(shadow_map_resolution),
            0.0f, 1.0f);
        nw::gfx::cmd_set_scissor(cmd, 0, 0, shadow_map_resolution, shadow_map_resolution);
        for (size_t candidate_index = 0; candidate_index < shadow_render_models.size(); ++candidate_index) {
            const auto& candidate = shadow_render_models[candidate_index];
            if (!bounds_intersects_shadow_clip(candidate.bounds, shadow.world_to_shadow[cascade])) {
                continue;
            }
            if (candidate.model_index >= scene.static_models.size()
                || !scene.static_models[candidate.model_index]) {
                continue;
            }
            if (candidate.prepared_draw_range_index >= shadow_draws->ranges.ranges.size()) {
                continue;
            }
            if (!prepared_shadow_range_casts_shadow(
                    shadow_range_casts_shadow,
                    candidate.prepared_draw_range_index)) {
                continue;
            }
            nw::render::render_prepared_render_model_shadow_surfaces(
                render_model_ctx,
                cmd,
                *scene.static_models[candidate.model_index],
                shadow_surface_span,
                candidate.prepared_draw_range_index,
                glm::mat4(1.0f),
                shadow.world_to_shadow[cascade],
                &shadow_surfaces->render_model_skins,
                &scene.material_overrides);
        }
        nw::gfx::cmd_end_render(cmd);
    }

    if (stats) {
        *stats = local_stats;
    }
    return true;
}

SceneLocalShadows resolve_local_shadows(
    std::span<LocalLight> lights,
    std::optional<std::span<const uint32_t>> candidate_light_indices)
{
    SceneLocalShadows result{};
    result.strength = kLocalShadowStrength;

    struct Candidate {
        uint32_t index = 0;
        float score = 0.0f;
    };
    for (LocalLight& light : lights) {
        light.shadow_slot = -1;
    }

    const auto candidate_count = candidate_light_indices ? candidate_light_indices->size() : lights.size();
    std::vector<Candidate> candidates;
    candidates.reserve(candidate_count);
    const auto append_candidate = [&](uint32_t i) {
        if (i >= lights.size()) {
            return;
        }
        LocalLight& light = lights[i];
        // Select relevant lights within the K-slot cost budget, ambient-contribution
        // included — in NWN interiors that path carries the room's illumination, so
        // excluding it means nothing casts. NWN tile/lantern lights almost never set
        // the authored casts_shadow flag, so it is a priority boost, not a hard gate.
        if (light.intensity <= 1.0e-4f || light.radius <= 1.0e-3f) {
            return;
        }
        const float score = local_shadow_candidate_score(light);
        if (score <= 0.0f) {
            return;
        }
        candidates.push_back(Candidate{i, score});
    };

    if (candidate_light_indices) {
        for (const uint32_t i : *candidate_light_indices) {
            append_candidate(i);
        }
    } else {
        for (uint32_t i = 0; i < lights.size(); ++i) {
            append_candidate(i);
        }
    }

    if (candidates.empty()) {
        return result;
    }

    const uint32_t count = std::min<uint32_t>(
        saturating_count(candidates.size()), kLocalShadowCount);
    std::partial_sort(
        candidates.begin(), candidates.begin() + count, candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.score == b.score) {
                return a.index < b.index;
            }
            return a.score > b.score;
        });

    result.count = count;
    for (uint32_t slot = 0; slot < count; ++slot) {
        LocalLight& light = lights[candidates[slot].index];
        light.shadow_slot = static_cast<int32_t>(slot);
        result.world_to_shadow[slot] = local_shadow_world_to_shadow(light);
    }
    return result;
}

bool render_local_shadow_maps(
    nw::render::RenderService& render_service,
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    SceneLocalShadows& local_shadows,
    uint32_t shadow_map_resolution,
    LocalShadowRenderStats* stats,
    AreaRenderFrame* area_frame,
    const PreviewPreparedModelDraws* frame_prepared_model_draws,
    const nw::render::PreparedModelSurfaceDrawList* frame_prepared_model_surfaces)
{
    LocalShadowRenderStats local_stats{};
    if (!cmd || local_shadows.count == 0) {
        if (stats) {
            *stats = local_stats;
        }
        return false;
    }

    auto& local_shadow_renderer = render_service.local_shadow_renderer();
    const auto shadow_pipeline = render_model_ctx.gpu
        ? render_model_ctx.gpu->pipeline({
              .mesh = nw::render::ModelPipelineMeshKind::pbr_static,
              .material = nw::render::MaterialMode::opaque,
              .pass = nw::render::ModelPipelinePass::shadow,
          })
        : nw::gfx::Handle<nw::gfx::Pipeline>{};
    if (!render_model_ctx.gpu || !shadow_pipeline.valid()
        || !local_shadow_renderer.ensure_resources(shadow_map_resolution)) {
        local_shadows.count = 0;
        if (stats) {
            *stats = local_stats;
        }
        return false;
    }

    local_stats.caster_light_count = local_shadows.count;
    std::vector<ShadowRenderModelCandidate> render_model_casters;
    std::vector<nw::render::ModelInstanceHandle> render_model_caster_handles;
    std::vector<uint32_t> render_model_candidate_by_source;
    if (scene.area_render_scene) {
        const auto& area_scene = *scene.area_render_scene;
        std::vector<uint32_t> local_shadow_caster_records;
        std::span<const uint32_t> shadow_caster_records;
        if (area_frame) {
            shadow_caster_records = area_frame->shadow_caster_record_indices();
        } else {
            const auto flags = area_scene.flags();
            local_shadow_caster_records.reserve(flags.size());
            for (uint32_t record_index = 0; record_index < flags.size(); ++record_index) {
                if (area_record_has_flag(flags[record_index], AreaRenderScene::RecordFlag::render_enabled)
                    && area_record_has_flag(flags[record_index], AreaRenderScene::RecordFlag::shadow_caster)) {
                    local_shadow_caster_records.push_back(record_index);
                }
            }
            shadow_caster_records = local_shadow_caster_records;
        }
        collect_area_shadow_render_model_candidates(
            render_model_casters,
            render_model_candidate_by_source,
            &render_model_caster_handles,
            scene,
            area_scene,
            shadow_caster_records);
    } else {
        render_model_casters.reserve(scene.static_models.size());
        render_model_caster_handles.reserve(scene.static_models.size());
        render_model_candidate_by_source.assign(
            scene.static_models.size(), std::numeric_limits<uint32_t>::max());
        for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
            if (model_index >= scene.static_model_instance_handles.size()
                || model_index > std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            const auto& model = scene.static_models[model_index];
            const auto* instance = scene.static_model_instance(model_index);
            if (!model || !instance || !instance->visible || !instance->shadow.casts_shadow) {
                continue;
            }
            render_model_casters.push_back(ShadowRenderModelCandidate{
                .model_index = static_cast<uint32_t>(model_index),
                .bounds = instance->shadow.bounds,
            });
            render_model_candidate_by_source[model_index]
                = static_cast<uint32_t>(render_model_casters.size() - 1u);
            render_model_caster_handles.push_back(scene.static_model_instance_handles[model_index]);
        }
    }

    PreviewPreparedModelDraws local_shadow_draws;
    nw::render::PreparedModelSurfaceDrawList local_shadow_surfaces;
    const PreviewPreparedModelDraws* shadow_draws = frame_prepared_model_draws;
    const nw::render::PreparedModelSurfaceDrawList* shadow_surfaces = frame_prepared_model_surfaces;
    if (!shadow_draws || !shadow_surfaces) {
        if (!render_model_caster_handles.empty()) {
            collect_prepared_model_draws(
                local_shadow_draws,
                scene,
                std::span<const nw::render::ModelInstanceHandle>{render_model_caster_handles});
            nw::render::collect_prepared_model_surface_draws(
                local_shadow_surfaces,
                local_shadow_draws.common,
                local_shadow_draws.ranges,
                scene.model_instances);
        }
        shadow_draws = &local_shadow_draws;
        shadow_surfaces = &local_shadow_surfaces;
    }

    std::vector<uint8_t> render_model_shadow_range_casts_shadow;
    nw::render::collect_prepared_model_surface_shadow_ranges(
        render_model_shadow_range_casts_shadow,
        std::span<const nw::render::PreparedModelSurfaceDraw>{
            shadow_surfaces->draws.data(),
            shadow_surfaces->draws.size()},
        shadow_draws->ranges.ranges.size());
    const std::span<const nw::render::PreparedModelSurfaceDraw> render_model_shadow_surfaces{
        shadow_surfaces->draws.data(),
        shadow_surfaces->draws.size()};
    assign_shadow_prepared_ranges(
        render_model_casters,
        render_model_candidate_by_source,
        shadow_draws->ranges);
    drop_non_shadow_prepared_ranges(render_model_casters, render_model_shadow_range_casts_shadow);

    const auto begin_slot = [&](uint32_t slot) {
        local_shadows.depth_textures[slot] = local_shadow_renderer.depth_texture(slot);
        nw::gfx::cmd_begin_render(cmd, local_shadow_renderer.render_target(slot));
        nw::gfx::cmd_set_viewport(cmd, 0.0f, 0.0f,
            static_cast<float>(shadow_map_resolution),
            static_cast<float>(shadow_map_resolution),
            0.0f, 1.0f);
        nw::gfx::cmd_set_scissor(cmd, 0, 0, shadow_map_resolution, shadow_map_resolution);
    };

    for (uint32_t slot = 0; slot < local_shadows.count; ++slot) {
        const glm::mat4& world_to_shadow = local_shadows.world_to_shadow[slot];
        begin_slot(slot);

        for (const auto& caster : render_model_casters) {
            if (!bounds_intersects_shadow_clip(caster.bounds, world_to_shadow)) {
                ++local_stats.culled_model_count;
                continue;
            }
            if (caster.model_index >= scene.static_models.size()
                || !scene.static_models[caster.model_index]) {
                continue;
            }
            ++local_stats.submitted_model_count;
            nw::render::render_prepared_render_model_shadow_surfaces(
                render_model_ctx,
                cmd,
                *scene.static_models[caster.model_index],
                render_model_shadow_surfaces,
                caster.prepared_draw_range_index,
                glm::mat4(1.0f),
                world_to_shadow,
                &shadow_surfaces->render_model_skins,
                &scene.material_overrides);
        }
        nw::gfx::cmd_end_render(cmd);
    }

    if (stats) {
        *stats = local_stats;
    }
    return true;
}

} // namespace nw::render::viewer
