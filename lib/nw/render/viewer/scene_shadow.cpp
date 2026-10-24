#include "scene_shadow.hpp"

#include "preview_model_animation.hpp"
#include "preview_model_draws.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/log.hpp>
#include <nw/render/local_shadow_renderer.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/shadow_culling.hpp>
#include <nw/render/shadow_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
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

float shadow_cascade_blend_width(float previous_split_distance, float split_distance) noexcept
{
    const float span = std::max(split_distance - previous_split_distance, 1.0f);
    return std::min(kShadowCascadeBlendDistance, span * kShadowCascadeBlendFraction);
}

enum class ShadowPreparedDrawSource : uint8_t {
    frame,
    area_cache,
};

struct ShadowRenderModelCandidate {
    uint32_t model_index = 0;
    uint32_t prepared_draw_range_index = std::numeric_limits<uint32_t>::max();
    uint32_t area_record_index = kInvalidAreaRenderRecordIndex;
    Bounds bounds{};
    ShadowPreparedDrawSource prepared_draw_source = ShadowPreparedDrawSource::frame;
    // Frame-local culling results, shared by statistics and draw submission.
    uint8_t visible_pass_mask = 0u;
};

static_assert(kShadowCascadeCount <= 8u && kLocalShadowCount <= 8u);

// Pack the strided candidate bounds once; the shared culler reads only these
// contiguous hot rows. Candidate/model/surface ownership is unchanged.
uint64_t cull_shadow_candidates(std::span<ShadowRenderModelCandidate> candidates,
    std::span<const glm::mat4> matrices)
{
    std::vector<Bounds> bounds;
    bounds.reserve(candidates.size());
    for (const auto& candidate : candidates)
        bounds.push_back(candidate.bounds);
    std::vector<uint8_t> masks(candidates.size());
    CHECK_F(collect_shadow_visibility_masks(bounds, matrices, masks), "Invalid shadow visibility batch shape");
    uint64_t submitted = 0u;
    for (size_t index = 0; index < candidates.size(); ++index) {
        candidates[index].visible_pass_mask = masks[index];
        submitted += std::popcount(masks[index]);
    }
    return submitted;
}

// Area-static prepared payloads retain scene-cache lifetime; dynamic area and
// non-area payloads retain frame lifetime. Keep both flat sources in place and
// tag candidates instead of copying/rebasing cached ranges every frame.
struct ShadowPreparedDrawView {
    const nw::render::PreparedModelDrawRangeList* ranges = nullptr;
    const nw::render::PreparedModelSurfaceDrawList* surfaces = nullptr;
    std::span<const uint8_t> range_casts_shadow;
    std::span<const nw::render::PreparedModelSurfaceRangeBounds> surface_range_bounds;
};

uint32_t saturating_count(size_t value)
{
    return static_cast<uint32_t>(std::min<size_t>(value, std::numeric_limits<uint32_t>::max()));
}

uint32_t saturating_sum(uint32_t lhs, uint32_t rhs) noexcept
{
    const uint64_t sum = static_cast<uint64_t>(lhs) + rhs;
    return static_cast<uint32_t>(std::min<uint64_t>(sum, std::numeric_limits<uint32_t>::max()));
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
    const nw::render::PreparedModelDrawRangeList& ranges,
    ShadowPreparedDrawSource prepared_draw_source)
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
            candidates[candidate_index].prepared_draw_source = prepared_draw_source;
        }
    }
}

template <typename Candidate>
uint32_t drop_non_shadow_prepared_ranges(
    std::vector<Candidate>& candidates,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws)
{
    uint32_t dropped_count = 0;
    auto removed_begin = std::remove_if(
        candidates.begin(),
        candidates.end(),
        [&](const auto& candidate) {
            const auto& draws = candidate.prepared_draw_source == ShadowPreparedDrawSource::area_cache
                ? area_cached_draws
                : frame_draws;
            if (draws.ranges && draws.surfaces
                && prepared_shadow_range_casts_shadow(
                    draws.range_casts_shadow,
                    candidate.prepared_draw_range_index)) {
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
// range matching or Vulkan submission. Only dynamic handles are emitted for
// fallback collection because static prepared payloads are area-cache owned.
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
    const auto flags = area_scene.flags();

    for (const uint32_t record_index : record_indices) {
        if (record_index >= model_indices.size()
            || record_index >= model_instance_handles.size()
            || record_index >= flags.size()) {
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
            .area_record_index = record_index,
            .bounds = area_shadow_record_bounds(area_scene, record_index),
        });
        candidate_by_source[model_index] = static_cast<uint32_t>(out.size() - 1u);
        if (handles
            && !area_record_has_flag(
                flags[record_index],
                AreaRenderScene::RecordFlag::static_candidate)) {
            handles->push_back(instance_handle);
        }
    }
}

glm::mat4 shadow_normal_matrix(const glm::mat4& model_matrix)
{
    return glm::mat4(glm::mat3(glm::transpose(glm::inverse(model_matrix))));
}

const ShadowPreparedDrawView& shadow_prepared_draw_view(
    const ShadowRenderModelCandidate& candidate,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws) noexcept
{
    return candidate.prepared_draw_source
            == ShadowPreparedDrawSource::area_cache
        ? area_cached_draws
        : frame_draws;
}

std::span<const nw::render::PreparedModelSurfaceDraw>
shadow_candidate_surfaces(
    const PreviewScene& scene,
    const ShadowRenderModelCandidate& candidate,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws)
{
    const auto& prepared_draws = shadow_prepared_draw_view(
        candidate, frame_draws, area_cached_draws);
    if (!prepared_draws.surfaces) {
        return {};
    }
    if (candidate.prepared_draw_source
        == ShadowPreparedDrawSource::area_cache) {
        if (!scene.area_render_scene
            || candidate.area_record_index
                == kInvalidAreaRenderRecordIndex) {
            return {};
        }
        return scene.area_render_scene
            ->prepared_model_surface_draws_for_record(
                candidate.area_record_index);
    }
    return nw::render::prepared_model_surface_draws_for_range(
        prepared_draws.surfaces->draws, prepared_draws.surface_range_bounds,
        candidate.prepared_draw_range_index);
}

bool shadow_batch_surface_compatible(
    const nw::render::PreparedModelSurfaceDraw& left,
    const nw::render::PreparedModelSurfaceDraw& right) noexcept
{
    return left.source_draw_index == right.source_draw_index
        && left.material_index == right.material_index
        && left.material_override == right.material_override
        && left.skin_index == right.skin_index
        && left.material_mode == right.material_mode
        && left.material_uses_fallback
        == right.material_uses_fallback
        && left.material_payload == right.material_payload
        && left.skinned == right.skinned;
}

bool shadow_batch_surfaces_compatible(
    std::span<const nw::render::PreparedModelSurfaceDraw> left,
    std::span<const nw::render::PreparedModelSurfaceDraw> right)
{
    size_t left_index = 0u;
    size_t right_index = 0u;
    while (true) {
        while (left_index < left.size()
            && !left[left_index].casts_shadow) {
            ++left_index;
        }
        while (right_index < right.size()
            && !right[right_index].casts_shadow) {
            ++right_index;
        }
        if (left_index == left.size()
            || right_index == right.size()) {
            return left_index == left.size()
                && right_index == right.size();
        }
        if (!shadow_batch_surface_compatible(
                left[left_index], right[right_index])) {
            return false;
        }
        ++left_index;
        ++right_index;
    }
}

bool shadow_candidate_batch_eligible(
    const nw::render::ModelRenderContext& render_model_ctx,
    const PreviewScene& scene,
    const ShadowRenderModelCandidate& candidate,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws)
{
    if (!render_model_ctx.gpu
        || candidate.prepared_draw_source
            != ShadowPreparedDrawSource::area_cache
        || candidate.model_index >= scene.static_models.size()
        || !scene.static_models[candidate.model_index]) {
        return false;
    }
    const auto* instance
        = scene.static_model_instance(candidate.model_index);
    if (!instance || instance->scene_animation_enabled) {
        return false;
    }

    const auto surfaces = shadow_candidate_surfaces(
        scene, candidate, frame_draws, area_cached_draws);
    bool has_shadow_surface = false;
    for (const auto& surface : surfaces) {
        if (!surface.casts_shadow) {
            continue;
        }
        has_shadow_surface = true;
        if (surface.skinned || surface.material_override.valid()
            || !nw::render::prepared_render_model_shadow_surface_matches_model(
                *scene.static_models[candidate.model_index], surface)) {
            return false;
        }
        const auto pipeline = render_model_ctx.gpu->pipeline({
            .mesh = nw::render::ModelPipelineMeshKind::pbr_static_instanced,
            .material = surface.material_mode,
            .pass = nw::render::ModelPipelinePass::shadow,
        });
        if (!pipeline.valid()) {
            return false;
        }
    }
    return has_shadow_surface;
}

// Per-call batch protocol: candidate_indices are indices into the immutable
// flat candidate input; groups own only transient indices and never retain
// model or surface pointers. Root groups retain static-cache compatibility;
// world groups use current frame surfaces. Invalid, skinned, deforming,
// overridden, or pipeline-incompatible frame candidates remain scalar.
struct ShadowSurfaceBatchGroup {
    uint32_t representative_candidate_index = 0u;
    PreparedModelSurfaceInstanceSpace space = PreparedModelSurfaceInstanceSpace::model_root;
    std::vector<uint32_t> candidate_indices;
};

struct ShadowSurfaceBatches {
    std::vector<ShadowSurfaceBatchGroup> groups;
    std::vector<uint32_t> fallback_candidate_indices;
    // Exact source indices partitioned by prepared range, preserving order.
    // Frame-local; invalid ranges are omitted. No source payloads are copied.
    std::vector<uint32_t> frame_surface_indices;
    std::vector<PreparedModelSurfaceRangeBounds> frame_surface_ranges;
};

ShadowSurfaceBatches compile_shadow_static_batches(
    const nw::render::ModelRenderContext& render_model_ctx,
    const PreviewScene& scene,
    std::span<const ShadowRenderModelCandidate> candidates,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws)
{
    ShadowSurfaceBatches result;
    result.groups.reserve(candidates.size());
    result.fallback_candidate_indices.reserve(candidates.size());
    for (size_t candidate_index = 0u;
        candidate_index < candidates.size(); ++candidate_index) {
        if (candidate_index > std::numeric_limits<uint32_t>::max()) {
            break;
        }
        const auto& candidate = candidates[candidate_index];
        if (!shadow_candidate_batch_eligible(render_model_ctx,
                scene, candidate, frame_draws,
                area_cached_draws)) {
            result.fallback_candidate_indices.push_back(
                static_cast<uint32_t>(candidate_index));
            continue;
        }

        const auto& model
            = scene.static_models[candidate.model_index];
        const auto surfaces = shadow_candidate_surfaces(
            scene, candidate, frame_draws,
            area_cached_draws);
        auto group = std::find_if(result.groups.begin(),
            result.groups.end(), [&](const auto& entry) {
                const auto& representative
                    = candidates[entry
                            .representative_candidate_index];
                return scene.static_models[representative.model_index]
                    == model
                    && shadow_batch_surfaces_compatible(
                        shadow_candidate_surfaces(scene,
                            representative, frame_draws,
                            area_cached_draws),
                        surfaces);
            });
        if (group == result.groups.end()) {
            result.groups.push_back({
                .representative_candidate_index
                = static_cast<uint32_t>(candidate_index),
                .candidate_indices = {
                    static_cast<uint32_t>(candidate_index)},
            });
        } else {
            group->candidate_indices.push_back(
                static_cast<uint32_t>(candidate_index));
        }
    }
    return result;
}

ShadowSurfaceBatches build_shadow_static_batches(
    const nw::render::ModelRenderContext& render_model_ctx,
    const PreviewScene& scene,
    std::span<const ShadowRenderModelCandidate> candidates,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws)
{
    if (!scene.area_render_scene || !area_cached_draws.surfaces) {
        return compile_shadow_static_batches(
            render_model_ctx, scene, candidates, frame_draws, area_cached_draws);
    }

    uint8_t pipeline_mask = 0u;
    constexpr std::array modes{MaterialMode::opaque, MaterialMode::cutout,
        MaterialMode::transparent, MaterialMode::water};
    if (render_model_ctx.gpu) {
        for (size_t index = 0; index < modes.size(); ++index) {
            if (render_model_ctx.gpu->pipeline({
                                                   .mesh = ModelPipelineMeshKind::pbr_static_instanced,
                                                   .material = modes[index],
                                                   .pass = ModelPipelinePass::shadow,
                                               })
                    .valid()) {
                pipeline_mask |= static_cast<uint8_t>(1u << index);
            }
        }
    }

    auto& area = *scene.area_render_scene;
    auto& cache = area.shadow_batch_cache();
    if (!cache.valid || cache.static_generation != area.static_cache_generation()
        || cache.tile_generation != area.tile_cache_generation()
        || cache.pipeline_mask != pipeline_mask) {
        const auto models = area.model_indices();
        std::vector<ShadowRenderModelCandidate> all_candidates;
        all_candidates.reserve(models.size());
        for (uint32_t record = 0; record < models.size(); ++record) {
            all_candidates.push_back({
                .model_index = models[record],
                .area_record_index = record,
                .prepared_draw_source = ShadowPreparedDrawSource::area_cache,
            });
        }
        const auto compiled = compile_shadow_static_batches(
            render_model_ctx, scene, all_candidates, frame_draws, area_cached_draws);
        cache.model_group_indices.assign(scene.static_models.size(), kInvalidAreaRenderRecordIndex);
        cache.group_model_counts.clear();
        cache.group_model_counts.reserve(compiled.groups.size());
        for (uint32_t group_index = 0; group_index < compiled.groups.size(); ++group_index) {
            const auto& group = compiled.groups[group_index];
            cache.group_model_counts.push_back(saturating_count(group.candidate_indices.size()));
            for (const uint32_t candidate_index : group.candidate_indices) {
                cache.model_group_indices[all_candidates[candidate_index].model_index] = group_index;
            }
        }
        cache.static_generation = area.static_cache_generation();
        cache.tile_generation = area.tile_cache_generation();
        cache.pipeline_mask = pipeline_mask;
        ++cache.rebuild_count;
        cache.valid = true;
    }

    ShadowSurfaceBatches result;
    result.groups.reserve(cache.group_model_counts.size());
    result.fallback_candidate_indices.reserve(candidates.size());
    std::vector<uint32_t> frame_groups(cache.group_model_counts.size(), kInvalidAreaRenderRecordIndex);
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index > std::numeric_limits<uint32_t>::max()) break;
        const uint32_t candidate_index = static_cast<uint32_t>(index);
        const auto& candidate = candidates[index];
        const uint32_t cached_group = candidate.model_index < cache.model_group_indices.size()
            ? cache.model_group_indices[candidate.model_index]
            : kInvalidAreaRenderRecordIndex;
        const auto* instance = scene.static_model_instance(candidate.model_index);
        if (candidate.prepared_draw_source != ShadowPreparedDrawSource::area_cache
            || cached_group >= frame_groups.size()
            || !instance || instance->scene_animation_enabled) {
            result.fallback_candidate_indices.push_back(candidate_index);
            continue;
        }

        // First encountered visible member preserves the uncached builder's
        // group order and representative, including partially visible groups.
        auto& frame_group = frame_groups[cached_group];
        if (frame_group == kInvalidAreaRenderRecordIndex) {
            frame_group = static_cast<uint32_t>(result.groups.size());
            auto& group = result.groups.emplace_back();
            group.representative_candidate_index = candidate_index;
            group.candidate_indices.reserve(cache.group_model_counts[cached_group]);
        }
        result.groups[frame_group].candidate_indices.push_back(candidate_index);
    }
    return result;
}

std::span<const uint32_t> frame_shadow_surface_indices(
    const ShadowSurfaceBatches& batches, uint32_t range_index)
{
    if (range_index >= batches.frame_surface_ranges.size()) return {};
    const auto& range = batches.frame_surface_ranges[range_index];
    return std::span{batches.frame_surface_indices}.subspan(range.begin, range.end - range.begin);
}

// Stable counting partition: each range owns a contiguous slice of source
// indices, including when main-pass order interleaves its caster surfaces.
// The caller bounds source/range counts to uint32_t; invalid ranges are skipped.
void collect_frame_shadow_surface_indices(
    ShadowSurfaceBatches& batches,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    size_t range_count)
{
    auto& ranges = batches.frame_surface_ranges;
    ranges.assign(range_count, {});
    // Count caster rows in each range.
    for (const auto& surface : surfaces) {
        if (surface.casts_shadow && surface.range_index < ranges.size()) ++ranges[surface.range_index].end;
    }
    size_t total = 0;
    // Convert counts into empty output slices.
    for (auto& range : ranges) {
        const auto count = range.end;
        range.begin = total;
        range.end = total;
        total += count;
    }
    batches.frame_surface_indices.resize(total);
    // Fill each slice in the original source order.
    for (uint32_t index = 0; index < surfaces.size(); ++index) {
        const auto& surface = surfaces[index];
        if (surface.casts_shadow && surface.range_index < ranges.size()) {
            batches.frame_surface_indices[ranges[surface.range_index].end++] = index;
        }
    }
}

bool frame_shadow_surfaces_batch_eligible(
    const ModelRenderContext& render_model_ctx,
    const RenderModel& model,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    std::span<const uint32_t> indices)
{
    if (indices.empty()) return false;
    for (const auto index : indices) {
        const auto& surface = surfaces[index];
        if (surface.skinned || surface.material_override.valid()
            || !prepared_render_model_shadow_surface_matches_model(model, surface)
            || model.primitives[surface.source_draw_index].deformer != kInvalidModelDeformerIndex) {
            return false;
        }
        const auto pipeline = render_model_ctx.gpu->pipeline({
            .mesh = ModelPipelineMeshKind::pbr_static_instanced,
            .material = surface.material_mode,
            .pass = ModelPipelinePass::shadow,
        });
        if (!pipeline.valid()) return false;
    }
    return true;
}

bool indexed_shadow_surfaces_compatible(
    std::span<const PreparedModelSurfaceDraw> surfaces,
    std::span<const uint32_t> left,
    std::span<const uint32_t> right)
{
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (!shadow_batch_surface_compatible(surfaces[left[index]], surfaces[right[index]])) return false;
    }
    return true;
}

ShadowSurfaceBatches build_shadow_batches(
    const nw::render::ModelRenderContext& render_model_ctx,
    const PreviewScene& scene,
    std::span<const ShadowRenderModelCandidate> candidates,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws)
{
    auto result = build_shadow_static_batches(render_model_ctx, scene, candidates, frame_draws, area_cached_draws);
    if (!render_model_ctx.gpu || !frame_draws.surfaces || !frame_draws.ranges
        || result.fallback_candidate_indices.empty()
        || frame_draws.surfaces->draws.size() > std::numeric_limits<uint32_t>::max()
        || frame_draws.ranges->ranges.size() > std::numeric_limits<uint32_t>::max()) return result;

    const auto& surfaces = frame_draws.surfaces->draws;
    collect_frame_shadow_surface_indices(result, surfaces, frame_draws.ranges->ranges.size());

    const size_t root_group_count = result.groups.size();
    auto fallback_candidates = std::move(result.fallback_candidate_indices);
    result.fallback_candidate_indices.reserve(fallback_candidates.size());
    for (const auto candidate_index : fallback_candidates) {
        const auto& candidate = candidates[candidate_index];
        if (candidate.visible_pass_mask == 0u
            || candidate.prepared_draw_source != ShadowPreparedDrawSource::frame
            || candidate.model_index >= scene.static_models.size()) {
            result.fallback_candidate_indices.push_back(candidate_index);
            continue;
        }
        const auto& model = scene.static_models[candidate.model_index];
        const auto indices = frame_shadow_surface_indices(result, candidate.prepared_draw_range_index);
        if (!model || !frame_shadow_surfaces_batch_eligible(render_model_ctx, *model, surfaces, indices)) {
            result.fallback_candidate_indices.push_back(candidate_index);
            continue;
        }
        auto group = std::find_if(result.groups.begin() + root_group_count, result.groups.end(), [&](const auto& entry) {
            const auto& representative = candidates[entry.representative_candidate_index];
            return scene.static_models[representative.model_index] == model
                && indexed_shadow_surfaces_compatible(surfaces, indices,
                    frame_shadow_surface_indices(result, representative.prepared_draw_range_index));
        });
        if (group == result.groups.end()) {
            result.groups.push_back({
                .representative_candidate_index = candidate_index,
                .space = PreparedModelSurfaceInstanceSpace::primitive_world,
                .candidate_indices = {candidate_index},
            });
        } else {
            group->candidate_indices.push_back(candidate_index);
        }
    }
    return result;
}

struct ShadowBatchSubmissionStats {
    uint32_t batch_count = 0u;
    uint32_t batched_model_count = 0u;
    uint32_t batch_draw_count = 0u;
};

void render_shadow_candidate_scalar(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    const ShadowRenderModelCandidate& candidate,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws,
    const glm::mat4& light_view,
    const glm::mat4& light_projection)
{
    if (candidate.model_index >= scene.static_models.size()
        || !scene.static_models[candidate.model_index]) {
        return;
    }
    const auto& prepared_draws = shadow_prepared_draw_view(
        candidate, frame_draws, area_cached_draws);
    if (!prepared_draws.ranges || !prepared_draws.surfaces
        || candidate.prepared_draw_range_index
            >= prepared_draws.ranges->ranges.size()
        || !prepared_shadow_range_casts_shadow(
            prepared_draws.range_casts_shadow,
            candidate.prepared_draw_range_index)) {
        return;
    }
    const auto surfaces = shadow_candidate_surfaces(
        scene, candidate, frame_draws, area_cached_draws);
    nw::render::render_prepared_render_model_shadow_surfaces(
        render_model_ctx, cmd,
        *scene.static_models[candidate.model_index], surfaces,
        candidate.prepared_draw_range_index,
        light_view, light_projection,
        &prepared_draws.surfaces->render_model_skins,
        &scene.material_overrides);
}

ShadowBatchSubmissionStats render_shadow_candidates(
    const nw::render::ModelRenderContext& unprepared_render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const ShadowRenderModelCandidate> candidates,
    const ShadowSurfaceBatches& batches,
    const ShadowPreparedDrawView& frame_draws,
    const ShadowPreparedDrawView& area_cached_draws,
    const glm::mat4& light_view,
    const glm::mat4& light_projection,
    uint8_t visible_pass_bit)
{
    ShadowBatchSubmissionStats stats;
    SceneConstants view{};
    view.view = light_view;
    view.projection = light_projection;
    const auto render_model_ctx = prepare_render_model_view(unprepared_render_model_ctx, view);
    const auto visible = [&](uint32_t candidate_index) {
        return candidate_index < candidates.size()
            && (candidates[candidate_index].visible_pass_mask & visible_pass_bit) != 0u;
    };
    const auto render_scalar = [&](uint32_t candidate_index) {
        if (visible(candidate_index)) {
            render_shadow_candidate_scalar(render_model_ctx, cmd,
                scene, candidates[candidate_index], frame_draws,
                area_cached_draws, light_view,
                light_projection);
        }
    };

    for (const uint32_t candidate_index :
        batches.fallback_candidate_indices) {
        render_scalar(candidate_index);
    }

    std::vector<uint32_t> visible_candidate_indices;
    for (const auto& group : batches.groups) {
        visible_candidate_indices.clear();
        visible_candidate_indices.reserve(
            group.candidate_indices.size());
        for (const uint32_t candidate_index :
            group.candidate_indices) {
            if (visible(candidate_index)) {
                visible_candidate_indices.push_back(
                    candidate_index);
            }
        }
        if (visible_candidate_indices.size() < 2u || !render_model_ctx.gpu) {
            for (const auto candidate_index : visible_candidate_indices)
                render_scalar(candidate_index);
            continue;
        }
        const auto& representative = candidates[group.representative_candidate_index];
        const auto& model = *scene.static_models[representative.model_index];
        const bool world_space = group.space == PreparedModelSurfaceInstanceSpace::primitive_world;
        const auto world_indices = world_space ? frame_shadow_surface_indices(batches, representative.prepared_draw_range_index)
                                               : std::span<const uint32_t>{};
        const auto surfaces = world_space ? std::span<const PreparedModelSurfaceDraw>{}
                                          : shadow_candidate_surfaces(scene, representative, frame_draws, area_cached_draws);
        const size_t surface_count = world_space ? world_indices.size() : surfaces.size();
        const size_t slice_count = world_space ? surface_count : 1u;
        const size_t max_rows = std::numeric_limits<uint32_t>::max() / sizeof(PreparedModelSurfaceInstance);
        if (slice_count == 0u || visible_candidate_indices.size() > max_rows / slice_count) {
            for (const uint32_t candidate_index :
                visible_candidate_indices) {
                render_scalar(candidate_index);
            }
            continue;
        }

        const uint32_t instance_count
            = static_cast<uint32_t>(
                visible_candidate_indices.size());
        const uint32_t instance_bytes = static_cast<uint32_t>(instance_count * slice_count)
            * sizeof(nw::render::PreparedModelSurfaceInstance);
        const auto mapped_instances
            = render_model_ctx.gpu
                  ->allocate_mapped_frame_storage(
                      cmd, instance_bytes, 64u);
        if (!mapped_instances.span.buffer.valid()
            || !mapped_instances.data) {
            for (const uint32_t candidate_index :
                visible_candidate_indices) {
                render_scalar(candidate_index);
            }
            continue;
        }

        auto* instances = static_cast<
            nw::render::PreparedModelSurfaceInstance*>(
            mapped_instances.data);
        bool valid_instances = true;
        if (world_space) {
            for (size_t surface_index = 0; surface_index < surface_count; ++surface_index) {
                for (size_t instance_index = 0; instance_index < visible_candidate_indices.size(); ++instance_index) {
                    const auto& candidate = candidates[visible_candidate_indices[instance_index]];
                    const auto indices = frame_shadow_surface_indices(batches, candidate.prepared_draw_range_index);
                    const auto& surface = frame_draws.surfaces->draws[indices[surface_index]];
                    instances[surface_index * instance_count + instance_index] = {
                        .root = surface.world,
                        .root_normal_matrix = surface.normal_matrix,
                    };
                }
            }
        } else {
            for (size_t instance_index = 0; instance_index < visible_candidate_indices.size(); ++instance_index) {
                const auto& candidate = candidates[visible_candidate_indices[instance_index]];
                const auto* instance = scene.static_model_instance(candidate.model_index);
                if (!instance) {
                    valid_instances = false;
                    break;
                }
                instances[instance_index] = {
                    .root = instance->root_transform,
                    .root_normal_matrix = shadow_normal_matrix(instance->root_transform),
                };
            }
        }
        if (!valid_instances) {
            for (const uint32_t candidate_index :
                visible_candidate_indices) {
                render_scalar(candidate_index);
            }
            continue;
        }

        uint32_t batch_draw_count = 0u;
        for (size_t surface_index = 0; surface_index < surface_count; ++surface_index) {
            const auto& surface = world_space ? frame_draws.surfaces->draws[world_indices[surface_index]] : surfaces[surface_index];
            if (!surface.casts_shadow) {
                continue;
            }
            if (nw::render::render_prepared_render_model_shadow_surface_instances(
                    render_model_ctx, cmd, model, surface,
                    mapped_instances.span,
                    world_space ? static_cast<uint32_t>(surface_index * instance_count) : 0u, instance_count,
                    light_view, light_projection,
                    &scene.material_overrides, group.space)) {
                ++batch_draw_count;
            }
        }
        if (batch_draw_count > 0u) {
            ++stats.batch_count;
            stats.batched_model_count = saturating_sum(
                stats.batched_model_count, instance_count);
            stats.batch_draw_count = saturating_sum(
                stats.batch_draw_count, batch_draw_count);
        }
    }
    return stats;
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
    if (shadow.cascade_count == 0u || shadow.cascade_count > kShadowCascadeCount) {
        shadow.enabled = false;
        if (stats) {
            *stats = {};
        }
        return false;
    }
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

    std::vector<uint8_t> frame_shadow_range_casts_shadow;
    const auto frame_shadow_range_stats = nw::render::collect_prepared_model_surface_shadow_ranges(
        frame_shadow_range_casts_shadow,
        std::span<const nw::render::PreparedModelSurfaceDraw>{
            shadow_surfaces->draws.data(),
            shadow_surfaces->draws.size()},
        shadow_draws->ranges.ranges.size());
    std::vector<nw::render::PreparedModelSurfaceRangeBounds> frame_surface_range_bounds;
    nw::render::collect_prepared_model_surface_range_bounds(
        frame_surface_range_bounds, shadow_surfaces->draws, shadow_draws->ranges.ranges.size());
    const ShadowPreparedDrawView frame_shadow_draws{
        .ranges = &shadow_draws->ranges,
        .surfaces = shadow_surfaces,
        .range_casts_shadow = frame_shadow_range_casts_shadow,
        .surface_range_bounds = frame_surface_range_bounds,
    };
    assign_shadow_prepared_ranges(
        shadow_render_models,
        shadow_render_model_candidate_by_source,
        shadow_draws->ranges,
        ShadowPreparedDrawSource::frame);

    nw::render::PreparedModelSurfaceShadowRangeStats area_cached_shadow_range_stats{};
    ShadowPreparedDrawView area_cached_shadow_draws;
    if (scene.area_render_scene) {
        const auto& area_scene = *scene.area_render_scene;
        const auto& cached_ranges = area_scene.prepared_model_draw_ranges();
        const auto& cached_surfaces = area_scene.prepared_model_surface_draws();
        area_cached_shadow_range_stats = area_scene.prepared_shadow_range_stats();
        area_cached_shadow_draws = {
            .ranges = &cached_ranges,
            .surfaces = &cached_surfaces,
            .range_casts_shadow = area_scene.prepared_shadow_ranges(),
        };
        assign_shadow_prepared_ranges(
            shadow_render_models,
            shadow_render_model_candidate_by_source,
            cached_ranges,
            ShadowPreparedDrawSource::area_cache);
    }

    no_caster_model_count += drop_non_shadow_prepared_ranges(
        shadow_render_models,
        frame_shadow_draws,
        area_cached_shadow_draws);

    ShadowRenderStats local_stats{};
    local_stats.prepared_surface_shadow_range_count = saturating_sum(
        frame_shadow_range_stats.shadow_range_count,
        area_cached_shadow_range_stats.shadow_range_count);
    local_stats.prepared_surface_invalid_range_count = saturating_sum(
        frame_shadow_range_stats.invalid_range_index_count,
        area_cached_shadow_range_stats.invalid_range_index_count);

    local_stats.caster_model_count = saturating_count(shadow_render_models.size());
    local_stats.no_caster_model_count = no_caster_model_count;
    const uint64_t submitted = cull_shadow_candidates(shadow_render_models,
        std::span{shadow.world_to_shadow}.first(shadow.cascade_count));
    local_stats.submitted_model_count = saturating_count(submitted);
    local_stats.culled_model_count = saturating_count(
        shadow_render_models.size() * shadow.cascade_count - submitted);
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

    const auto shadow_batches
        = build_shadow_batches(render_model_ctx, scene,
            shadow_render_models, frame_shadow_draws,
            area_cached_shadow_draws);

    for (uint32_t cascade = 0; cascade < shadow.cascade_count; ++cascade) {
        ++local_stats.cascade_count;
        shadow.depth_textures[cascade] = shadow_renderer.depth_texture(cascade);
        nw::gfx::cmd_begin_render(cmd, shadow_renderer.render_target(cascade));
        nw::gfx::cmd_set_viewport(cmd, 0.0f, 0.0f,
            static_cast<float>(shadow_map_resolution),
            static_cast<float>(shadow_map_resolution),
            0.0f, 1.0f);
        nw::gfx::cmd_set_scissor(cmd, 0, 0, shadow_map_resolution, shadow_map_resolution);
        const auto batch_stats = render_shadow_candidates(
            render_model_ctx, cmd, scene,
            shadow_render_models, shadow_batches,
            frame_shadow_draws, area_cached_shadow_draws,
            glm::mat4(1.0f),
            shadow.world_to_shadow[cascade],
            static_cast<uint8_t>(1u << cascade));
        local_stats.static_batch_count = saturating_sum(
            local_stats.static_batch_count,
            batch_stats.batch_count);
        local_stats.static_batched_model_count
            = saturating_sum(
                local_stats.static_batched_model_count,
                batch_stats.batched_model_count);
        local_stats.static_batch_draw_count = saturating_sum(
            local_stats.static_batch_draw_count,
            batch_stats.batch_draw_count);
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
    if (local_shadows.count > kLocalShadowCount) {
        local_shadows.count = 0u;
    }
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

    std::vector<uint8_t> frame_shadow_range_casts_shadow;
    nw::render::collect_prepared_model_surface_shadow_ranges(
        frame_shadow_range_casts_shadow,
        std::span<const nw::render::PreparedModelSurfaceDraw>{
            shadow_surfaces->draws.data(),
            shadow_surfaces->draws.size()},
        shadow_draws->ranges.ranges.size());
    std::vector<nw::render::PreparedModelSurfaceRangeBounds> frame_surface_range_bounds;
    nw::render::collect_prepared_model_surface_range_bounds(
        frame_surface_range_bounds, shadow_surfaces->draws, shadow_draws->ranges.ranges.size());
    const ShadowPreparedDrawView frame_shadow_draws{
        .ranges = &shadow_draws->ranges,
        .surfaces = shadow_surfaces,
        .range_casts_shadow = frame_shadow_range_casts_shadow,
        .surface_range_bounds = frame_surface_range_bounds,
    };
    assign_shadow_prepared_ranges(
        render_model_casters,
        render_model_candidate_by_source,
        shadow_draws->ranges,
        ShadowPreparedDrawSource::frame);

    ShadowPreparedDrawView area_cached_shadow_draws;
    if (scene.area_render_scene) {
        const auto& area_scene = *scene.area_render_scene;
        const auto& cached_ranges = area_scene.prepared_model_draw_ranges();
        const auto& cached_surfaces = area_scene.prepared_model_surface_draws();
        area_cached_shadow_draws = {
            .ranges = &cached_ranges,
            .surfaces = &cached_surfaces,
            .range_casts_shadow = area_scene.prepared_shadow_ranges(),
        };
        assign_shadow_prepared_ranges(
            render_model_casters,
            render_model_candidate_by_source,
            cached_ranges,
            ShadowPreparedDrawSource::area_cache);
    }
    drop_non_shadow_prepared_ranges(
        render_model_casters,
        frame_shadow_draws,
        area_cached_shadow_draws);

    const uint64_t submitted = cull_shadow_candidates(render_model_casters,
        std::span{local_shadows.world_to_shadow}.first(local_shadows.count));
    local_stats.submitted_model_count = saturating_count(submitted);
    local_stats.culled_model_count = saturating_count(
        render_model_casters.size() * local_shadows.count - submitted);
    const auto shadow_batches
        = build_shadow_batches(render_model_ctx, scene,
            render_model_casters, frame_shadow_draws,
            area_cached_shadow_draws);

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

        const auto batch_stats = render_shadow_candidates(
            render_model_ctx, cmd, scene,
            render_model_casters, shadow_batches,
            frame_shadow_draws, area_cached_shadow_draws,
            glm::mat4(1.0f), world_to_shadow,
            static_cast<uint8_t>(1u << slot));
        local_stats.static_batch_count = saturating_sum(
            local_stats.static_batch_count,
            batch_stats.batch_count);
        local_stats.static_batched_model_count
            = saturating_sum(
                local_stats.static_batched_model_count,
                batch_stats.batched_model_count);
        local_stats.static_batch_draw_count = saturating_sum(
            local_stats.static_batch_draw_count,
            batch_stats.batch_draw_count);
        nw::gfx::cmd_end_render(cmd);
    }

    if (stats) {
        *stats = local_stats;
    }
    return true;
}

} // namespace nw::render::viewer
