#pragma once

#include <nw/render/model.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace nw::render {

inline constexpr uint32_t kInvalidPreparedModelDrawIndex = std::numeric_limits<uint32_t>::max();

enum class PreparedModelMaterialPayloadKind : uint8_t {
    pbr,
    plt,
    fallback,
};

[[nodiscard]] inline PreparedModelMaterialPayloadKind prepared_render_model_material_payload_kind(
    const Material& material) noexcept
{
    return material.material_uses_fallback
        ? PreparedModelMaterialPayloadKind::fallback
        : material.plt_enabled
        ? PreparedModelMaterialPayloadKind::plt
        : PreparedModelMaterialPayloadKind::pbr;
}

struct PreparedModelDraw {
    ModelInstanceHandle instance;
    uint32_t instance_source_index = kInvalidPreparedModelDrawIndex;
    uint32_t source_draw_index = kInvalidPreparedModelDrawIndex;
    uint32_t material_index = kInvalidPreparedModelDrawIndex;
    ModelMaterialOverrideHandle material_override;
    uint32_t skin_index = kInvalidPreparedModelDrawIndex;
    MaterialMode material_mode = MaterialMode::opaque;
    // Source adapter set when missing kind-specific material data was clamped to
    // a renderer fallback. Missing draw payloads still drop before this
    // record is emitted.
    bool material_uses_fallback = false;
    // Material payload class used to make PBR, PLT, and explicit fallback
    // decisions visible before backend-specific submission.
    PreparedModelMaterialPayloadKind material_payload = PreparedModelMaterialPayloadKind::pbr;
    bool skinned = false;
    // Instance-level shadow eligibility copied from ModelInstance::shadow.
    // Surface collection combines this with primitive and material eligibility.
    bool instance_casts_shadow = true;
    bool primitive_casts_shadow = true;
    glm::mat4 world{1.0f};
    glm::mat4 normal_matrix{1.0f};
    Bounds bounds{};
};

struct PreparedModelDrawStats {
    uint32_t handle_count = 0;
    uint32_t visible_instance_count = 0;
    uint32_t hidden_instance_count = 0;
    uint32_t stale_handle_count = 0;
    uint32_t missing_asset_count = 0;
    uint32_t invalid_draw_count = 0;
    uint32_t render_model_instance_count = 0;
    uint32_t render_model_draw_count = 0;
    uint32_t material_fallback_draw_count = 0;
    uint32_t render_model_material_fallback_draw_count = 0;
    uint32_t material_override_draw_count = 0;
    uint32_t invalid_material_override_handle_count = 0;
};

struct PreparedModelDrawList {
    std::vector<PreparedModelDraw> draws;
    // One entry per scene-owned input key plus a terminal offset. Model preview
    // collectors use stable ModelInstanceHandle order and apply visibility at
    // collection time. Area caches use stable area record order and mirror
    // static draw payloads independent of frame visibility. Invalid, missing,
    // stale, or dynamic inputs repeat the previous offset.
    std::vector<uint32_t> instance_offsets;
    PreparedModelDrawStats stats{};

    void clear()
    {
        draws.clear();
        instance_offsets.clear();
        stats = {};
    }
};

struct PreparedModelDrawRange {
    uint32_t handle_index = kInvalidPreparedModelDrawIndex;
    uint32_t draw_begin = kInvalidPreparedModelDrawIndex;
    uint32_t draw_count = 0;
    ModelInstanceHandle instance;
    uint32_t instance_source_index = kInvalidPreparedModelDrawIndex;
};

struct PreparedModelDrawRangeStats {
    uint32_t handle_count = 0;
    uint32_t empty_range_count = 0;
    uint32_t invalid_offset_range_count = 0;
    uint32_t mixed_range_count = 0;
    uint32_t range_count = 0;
    uint32_t draw_count = 0;
    uint32_t render_model_range_count = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return invalid_offset_range_count == 0
            && mixed_range_count == 0;
    }
};

struct PreparedModelDrawRangeList {
    std::vector<PreparedModelDrawRange> ranges;
    PreparedModelDrawRangeStats stats{};

    void clear()
    {
        ranges.clear();
        stats = {};
    }
};

// Smallest renderer-facing model surface command.
struct PreparedModelSurfaceDraw {
    ModelInstanceHandle instance;
    uint32_t range_index = kInvalidPreparedModelDrawIndex;
    uint32_t draw_index = kInvalidPreparedModelDrawIndex;
    uint32_t handle_index = kInvalidPreparedModelDrawIndex;
    uint32_t instance_source_index = kInvalidPreparedModelDrawIndex;
    uint32_t source_draw_index = kInvalidPreparedModelDrawIndex;
    uint32_t material_index = kInvalidPreparedModelDrawIndex;
    ModelMaterialOverrideHandle material_override;
    // Source asset skin index. Frame skin matrices are bound through an
    // instance-indexed table owned by the frame renderer, not by draw payloads.
    uint32_t skin_index = kInvalidPreparedModelDrawIndex;
    uint32_t skin_table_index = kInvalidPreparedModelDrawIndex;
    MaterialMode material_mode = MaterialMode::opaque;
    bool material_uses_fallback = false;
    PreparedModelMaterialPayloadKind material_payload = PreparedModelMaterialPayloadKind::pbr;
    bool skinned : 1 = false;
    bool casts_shadow : 1 = false;
    // Rebuilt together with world, including static-cache invalidation on edits.
    ModelTransformWinding winding = ModelTransformWinding::unknown;
    glm::mat4 world{1.0f};
    glm::mat4 normal_matrix{1.0f};
    Bounds bounds{};
};

static_assert(sizeof(PreparedModelSurfaceDraw) == 208);

using PreparedModelSurfaceSortKey = std::array<uint32_t, 10>;

// Keys remain in input order; order maps sorted positions to that input.
// Reuse is checked against every fresh key. No surface payload is retained.
struct PreparedModelSurfaceSortCache {
    std::vector<PreparedModelSurfaceSortKey> keys;
    std::vector<uint32_t> order;
    std::vector<uint32_t> permutation;
};

// Positions in one immutable surface batch. Interleaved ranges may include
// other ranges between begin/end; consumers must still filter by range_index.
// Empty ranges have begin == end. The bounds must be rebuilt after reordering.
struct PreparedModelSurfaceRangeBounds {
    size_t begin = 0;
    size_t end = 0;
};

enum class PreparedModelSurfaceInstanceSpace : uint8_t {
    model_root,
    primitive_world,
};

// Flat GPU row: two consecutive float4x4 values, owned by frame storage until
// GPU retirement. model_root rows combine with shared primitive-local draw
// constants; primitive_world rows contain complete prepared surface transforms
// and use identity primitive constants. A batch must use one space throughout.
struct PreparedModelSurfaceInstance {
    glm::mat4 root{1.0f};
    glm::mat4 root_normal_matrix{1.0f};
};

static_assert(sizeof(PreparedModelSurfaceInstance)
    == sizeof(glm::mat4) * 2u);
static_assert(std::is_trivially_copyable_v<PreparedModelSurfaceInstance>);

struct PreparedModelSurfaceDrawStats {
    uint32_t range_count = 0;
    uint32_t draw_count = 0;
    uint32_t render_model_draw_count = 0;
    uint32_t shadow_caster_draw_count = 0;
    uint32_t invalid_range_count = 0;
    uint32_t range_mismatch_count = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return invalid_range_count == 0
            && range_mismatch_count == 0;
    }
};

struct PreparedModelSurfaceMaterialStats {
    uint32_t opaque_count = 0;
    uint32_t cutout_count = 0;
    uint32_t water_count = 0;
    uint32_t transparent_count = 0;
    uint32_t invalid_mode_count = 0;

    [[nodiscard]] uint32_t total() const noexcept
    {
        const uint64_t total = static_cast<uint64_t>(opaque_count)
            + static_cast<uint64_t>(cutout_count)
            + static_cast<uint64_t>(water_count)
            + static_cast<uint64_t>(transparent_count)
            + static_cast<uint64_t>(invalid_mode_count);
        return total > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(total);
    }
};

struct PreparedModelSurfaceMaterialBindingStats {
    uint32_t surface_count = 0;
    uint32_t bound_surface_count = 0;
    uint32_t invalid_range_count = 0;
    uint32_t invalid_draw_index_count = 0;
    uint32_t range_mismatch_count = 0;
    uint32_t payload_mismatch_count = 0;
    uint32_t material_mismatch_count = 0;
    uint32_t material_fallback_count = 0;
    uint32_t render_model_material_fallback_count = 0;
    uint32_t fallback_material_payload_count = 0;
    uint32_t render_model_fallback_material_payload_count = 0;
    uint32_t material_override_surface_count = 0;
    uint32_t shadow_mismatch_count = 0;
    PreparedModelSurfaceMaterialStats materials{};

    [[nodiscard]] bool valid() const noexcept
    {
        return invalid_range_count == 0
            && invalid_draw_index_count == 0
            && range_mismatch_count == 0
            && payload_mismatch_count == 0
            && material_mismatch_count == 0
            && material_fallback_count == 0
            && fallback_material_payload_count == 0
            && shadow_mismatch_count == 0
            && materials.invalid_mode_count == 0;
    }
};

struct PreparedModelSurfaceShadowRangeStats {
    uint32_t range_count = 0;
    uint32_t surface_count = 0;
    uint32_t shadow_surface_count = 0;
    uint32_t shadow_range_count = 0;
    uint32_t invalid_range_index_count = 0;

    [[nodiscard]] bool valid() const noexcept { return invalid_range_index_count == 0; }
};

struct PreparedRenderModelSkinBindingStats {
    uint32_t skinned_surface_count = 0;
    uint32_t table_bound_surface_count = 0;
    uint32_t bind_pose_fallback_surface_count = 0;
    uint32_t missing_table_surface_count = 0;
    uint32_t invalid_table_index_surface_count = 0;
    uint32_t invalid_table_range_surface_count = 0;

    [[nodiscard]] uint32_t fallback_surface_count() const noexcept
    {
        const uint64_t total = static_cast<uint64_t>(bind_pose_fallback_surface_count)
            + static_cast<uint64_t>(missing_table_surface_count)
            + static_cast<uint64_t>(invalid_table_index_surface_count)
            + static_cast<uint64_t>(invalid_table_range_surface_count);
        return total > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(total);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return missing_table_surface_count == 0
            && invalid_table_index_surface_count == 0
            && invalid_table_range_surface_count == 0;
    }
};

struct PreparedRenderModelSurfaceSubmissionStats {
    uint32_t submitted_surface_count = 0;
    uint32_t dropped_invalid_surface_count = 0;
    uint32_t submitted_run_count = 0;
    uint32_t opaque_cutout_submitted_run_count = 0;
    uint32_t water_submitted_run_count = 0;
    uint32_t transparent_submitted_run_count = 0;
    uint32_t all_submitted_run_count = 0;
    PreparedRenderModelSkinBindingStats skin_bindings{};

    [[nodiscard]] bool valid() const noexcept
    {
        return dropped_invalid_surface_count == 0 && skin_bindings.valid();
    }
};

struct PreparedRenderModelSurfacePacketStats {
    uint32_t input_surface_count = 0;
    uint32_t pass_filtered_surface_count = 0;
    uint32_t candidate_surface_count = 0;
    uint32_t valid_surface_count = 0;
    uint32_t invalid_surface_count = 0;
    uint32_t invalid_surface_index_count = 0;
    uint32_t invalid_source_index_count = 0;
    uint32_t invalid_source_draw_index_count = 0;
    uint32_t invalid_material_index_count = 0;
    uint32_t invalid_material_override_handle_count = 0;
    uint32_t primitive_mismatch_count = 0;
    uint32_t material_mismatch_count = 0;
    PreparedRenderModelSkinBindingStats skin_bindings{};

    [[nodiscard]] bool valid() const noexcept
    {
        return invalid_surface_count == 0 && skin_bindings.valid();
    }
};

struct PreparedRenderModelSurfacePacketList {
    // Batch transform contract:
    // input layout: caller-owned RenderModel PreparedModelSurfaceDraw span,
    // normally one run from PreparedRenderModelSurfaceRunList.
    // output layout: indices into the input span for surfaces that are valid
    // for the selected RenderModel asset and render pass. The caller owns the
    // input span and it must outlive this list.
    // valid ranges: each output index is < input span size. Out-of-range
    // primitive/material payloads are dropped and counted before submission.
    std::vector<uint32_t> surface_indices;
    PreparedRenderModelSurfacePacketStats stats{};

    void clear()
    {
        surface_indices.clear();
        stats = {};
    }
};

struct PreparedRenderModelSkinRange {
    ModelInstanceHandle instance;
    uint32_t source_skin_index = kInvalidPreparedModelDrawIndex;
    uint32_t matrix_begin = 0;
    uint32_t matrix_count = 0;
};

struct PreparedRenderModelSkinTableStats {
    uint32_t input_surface_count = 0;
    uint32_t render_model_skinned_surface_count = 0;
    uint32_t assigned_surface_count = 0;
    uint32_t reused_surface_count = 0;
    uint32_t bind_pose_fallback_surface_count = 0;
    uint32_t unskinned_surface_count = 0;
    uint32_t stale_instance_count = 0;
    uint32_t invalid_skin_index_count = 0;
    uint32_t invalid_matrix_range_count = 0;
    uint32_t table_entry_count = 0;
    uint32_t matrix_count = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return stale_instance_count == 0
            && invalid_skin_index_count == 0
            && invalid_matrix_range_count == 0;
    }
};

struct PreparedRenderModelSkinTable {
    // Batch transform contract:
    // input layout: mutable caller-owned flat PreparedModelSurfaceDraw span.
    // output layout: frame-owned contiguous skin matrix storage plus ranges
    // indexed by PreparedModelSurfaceDraw::skin_table_index.
    // owner/lifetime: this table owns copied matrix values for the frame and
    // must outlive any renderer submission that reads skin_table_index.
    // valid ranges: skin_table_index is either invalid for unskinned, stale,
    // invalid, or bind-pose fallback surfaces, or it is
    // < ranges.size(). matrix_begin + matrix_count is <= matrices.size().
    std::vector<PreparedRenderModelSkinRange> ranges;
    std::vector<glm::mat4> matrices;
    PreparedRenderModelSkinTableStats stats{};

    void clear()
    {
        ranges.clear();
        matrices.clear();
        stats = {};
    }
};

struct PreparedModelSurfaceDrawList {
    std::vector<PreparedModelSurfaceDraw> draws;
    // Preserved by clear(); sorting validates against every fresh input key.
    PreparedModelSurfaceSortCache sort_cache;
    PreparedRenderModelSkinTable render_model_skins;
    PreparedRenderModelSurfacePacketList render_model_surface_packets;
    PreparedModelSurfaceDrawStats stats{};
    // Summary of the bindings emitted by collection. Reordering and skin-table
    // index updates preserve it. Call the independent validator after editing
    // binding fields or the source draw/range rows.
    PreparedModelSurfaceMaterialBindingStats material_bindings{};

    void clear()
    {
        draws.clear();
        render_model_skins.clear();
        render_model_surface_packets.clear();
        stats = {};
        material_bindings = {};
    }
};

struct PreparedModelSurfaceRun {
    std::span<const PreparedModelSurfaceDraw> draws;
    uint32_t instance_source_index = kInvalidPreparedModelDrawIndex;
    size_t next = 0;

    [[nodiscard]] bool empty() const noexcept { return draws.empty(); }
};

struct PreparedRenderModelSurfaceRunStats {
    uint32_t input_surface_count = 0;
    uint32_t render_model_surface_count = 0;
    uint32_t run_count = 0;
    uint32_t invalid_source_index_count = 0;

    [[nodiscard]] bool valid() const noexcept { return invalid_source_index_count == 0; }
};

struct PreparedRenderModelSurfaceRunList {
    // Batch transform contract:
    // input layout: caller-owned flat PreparedModelSurfaceDraw span, normally
    // sorted in-place by pass/payload/source before submission.
    // output layout: contiguous run views into the input span. Runs are owned
    // by this list; draw storage remains owned by the caller and must outlive
    // the list. instance_source_index is a stable index into the caller's
    // RenderModel source table.
    // valid ranges: source indices must be < render_model_source_count passed
    // to collect_prepared_render_model_surface_runs. Out-of-range sources are
    // dropped and counted.
    std::vector<PreparedModelSurfaceRun> runs;
    PreparedRenderModelSurfaceRunStats stats{};

    void clear()
    {
        runs.clear();
        stats = {};
    }
};

namespace detail {

[[nodiscard]] inline uint32_t prepared_model_draw_saturating_count(size_t value) noexcept
{
    return value > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(value);
}

inline void prepared_model_draw_add_saturating(uint32_t& target, uint32_t value) noexcept
{
    if (std::numeric_limits<uint32_t>::max() - target < value) {
        target = std::numeric_limits<uint32_t>::max();
        return;
    }
    target += value;
}

[[nodiscard]] inline bool prepared_model_draw_range_matches(
    const PreparedModelDraw& expected,
    const PreparedModelDraw& draw) noexcept
{
    return draw.instance == expected.instance
        && draw.instance_source_index == expected.instance_source_index;
}

[[nodiscard]] inline bool prepared_model_draw_matches_range(
    const PreparedModelDrawRange& range,
    const PreparedModelDraw& draw) noexcept
{
    return draw.instance == range.instance
        && draw.instance_source_index == range.instance_source_index;
}

[[nodiscard]] inline bool prepared_model_surface_matches_range(
    const PreparedModelSurfaceDraw& surface,
    const PreparedModelDrawRange& range) noexcept
{
    return surface.instance == range.instance
        && surface.handle_index == range.handle_index
        && surface.instance_source_index == range.instance_source_index;
}

inline void prepared_model_surface_add_material_stat(
    PreparedModelSurfaceMaterialStats& out,
    MaterialMode mode) noexcept
{
    switch (mode) {
    case MaterialMode::opaque:
        prepared_model_draw_add_saturating(out.opaque_count, 1u);
        break;
    case MaterialMode::cutout:
        prepared_model_draw_add_saturating(out.cutout_count, 1u);
        break;
    case MaterialMode::water:
        prepared_model_draw_add_saturating(out.water_count, 1u);
        break;
    case MaterialMode::transparent:
        prepared_model_draw_add_saturating(out.transparent_count, 1u);
        break;
    default:
        prepared_model_draw_add_saturating(out.invalid_mode_count, 1u);
        break;
    }
}

} // namespace detail

[[nodiscard]] inline std::span<const PreparedModelDraw> prepared_model_draws_for_handle_index(
    const PreparedModelDrawList& list,
    size_t handle_index) noexcept
{
    if (handle_index + 1 >= list.instance_offsets.size()) {
        return {};
    }

    const uint32_t begin = list.instance_offsets[handle_index];
    const uint32_t end = list.instance_offsets[handle_index + 1];
    if (begin > end || end > list.draws.size()) {
        return {};
    }
    return {list.draws.data() + begin, static_cast<size_t>(end - begin)};
}

[[nodiscard]] inline std::span<const PreparedModelDraw> prepared_model_draws_for_range(
    const PreparedModelDrawList& list,
    const PreparedModelDrawRange& range) noexcept
{
    if (range.draw_begin > list.draws.size()) {
        return {};
    }
    const size_t begin = range.draw_begin;
    const size_t count = range.draw_count;
    if (count > list.draws.size() - begin) {
        return {};
    }
    return {list.draws.data() + begin, count};
}

inline void collect_prepared_model_draw_ranges(
    PreparedModelDrawRangeList& out,
    const PreparedModelDrawList& list)
{
    out.clear();
    if (list.instance_offsets.empty()) {
        return;
    }

    const size_t handle_count = list.instance_offsets.size() - 1u;
    out.stats.handle_count = detail::prepared_model_draw_saturating_count(handle_count);
    out.ranges.reserve(handle_count);

    for (size_t handle_index = 0; handle_index < handle_count; ++handle_index) {
        const uint32_t begin = list.instance_offsets[handle_index];
        const uint32_t end = list.instance_offsets[handle_index + 1u];
        if (begin > end || end > list.draws.size() || handle_index > std::numeric_limits<uint32_t>::max()) {
            ++out.stats.invalid_offset_range_count;
            continue;
        }
        if (begin == end) {
            ++out.stats.empty_range_count;
            continue;
        }

        const auto& first = list.draws[begin];
        bool matches = true;
        for (uint32_t draw_index = begin + 1u; draw_index < end; ++draw_index) {
            if (!detail::prepared_model_draw_range_matches(first, list.draws[draw_index])) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            ++out.stats.mixed_range_count;
            continue;
        }

        const uint32_t draw_count = end - begin;
        out.ranges.push_back(PreparedModelDrawRange{
            .handle_index = static_cast<uint32_t>(handle_index),
            .draw_begin = begin,
            .draw_count = draw_count,
            .instance = first.instance,
            .instance_source_index = first.instance_source_index,
        });
        ++out.stats.range_count;
        detail::prepared_model_draw_add_saturating(out.stats.draw_count, draw_count);
        ++out.stats.render_model_range_count;
    }
}

inline void count_prepared_model_surface_materials(
    PreparedModelSurfaceMaterialStats& out,
    std::span<const PreparedModelSurfaceDraw> draws) noexcept
{
    for (const auto& draw : draws) {
        detail::prepared_model_surface_add_material_stat(out, draw.material_mode);
    }
}

[[nodiscard]] inline PreparedModelSurfaceMaterialStats prepared_model_surface_material_stats(
    std::span<const PreparedModelSurfaceDraw> draws) noexcept
{
    PreparedModelSurfaceMaterialStats result{};
    count_prepared_model_surface_materials(result, draws);
    return result;
}

// One row per range; invalid surface range indices are ignored. Owns no surface
// payloads and preserves source order, including pass-sorted/interleaved inputs.
inline void collect_prepared_model_surface_range_bounds(
    std::vector<PreparedModelSurfaceRangeBounds>& out,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    size_t range_count)
{
    out.assign(range_count, {});
    for (size_t index = 0; index < surfaces.size(); ++index) {
        const uint32_t range_index = surfaces[index].range_index;
        if (range_index == kInvalidPreparedModelDrawIndex || range_index >= out.size()) {
            continue;
        }
        auto& bounds = out[range_index];
        if (bounds.begin == bounds.end) {
            bounds.begin = index;
        }
        bounds.end = index + 1u;
    }
}

[[nodiscard]] inline std::span<const PreparedModelSurfaceDraw> prepared_model_surface_draws_for_range(
    std::span<const PreparedModelSurfaceDraw> surfaces,
    std::span<const PreparedModelSurfaceRangeBounds> bounds,
    uint32_t range_index) noexcept
{
    if (range_index == kInvalidPreparedModelDrawIndex || range_index >= bounds.size()) {
        return {};
    }
    const auto& range = bounds[range_index];
    if (range.begin >= range.end || range.end > surfaces.size()) {
        return {};
    }
    return surfaces.subspan(range.begin, range.end - range.begin);
}

inline PreparedModelSurfaceShadowRangeStats collect_prepared_model_surface_shadow_ranges(
    std::vector<uint8_t>& out,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    size_t range_count)
{
    out.assign(range_count, 0u);

    PreparedModelSurfaceShadowRangeStats stats{};
    stats.range_count = detail::prepared_model_draw_saturating_count(range_count);
    stats.surface_count = detail::prepared_model_draw_saturating_count(surfaces.size());

    for (const auto& surface : surfaces) {
        if (!surface.casts_shadow) {
            continue;
        }
        detail::prepared_model_draw_add_saturating(stats.shadow_surface_count, 1u);
        if (surface.range_index >= out.size()) {
            detail::prepared_model_draw_add_saturating(stats.invalid_range_index_count, 1u);
            continue;
        }
        if (out[surface.range_index] == 0u) {
            detail::prepared_model_draw_add_saturating(stats.shadow_range_count, 1u);
        }
        out[surface.range_index] = 1u;
    }

    return stats;
}

[[nodiscard]] inline bool prepared_model_surface_casts_shadow(MaterialMode mode) noexcept
{
    switch (mode) {
    case MaterialMode::opaque:
    case MaterialMode::cutout:
        return true;
    case MaterialMode::water:
    case MaterialMode::transparent:
        return false;
    }
    return false;
}

[[nodiscard]] inline bool prepared_model_surface_material_mode_valid(MaterialMode mode) noexcept
{
    switch (mode) {
    case MaterialMode::opaque:
    case MaterialMode::cutout:
    case MaterialMode::water:
    case MaterialMode::transparent:
        return true;
    }
    return false;
}

inline PreparedModelSurfaceMaterialBindingStats validate_prepared_model_surface_material_bindings(
    std::span<const PreparedModelSurfaceDraw> surfaces,
    const PreparedModelDrawList& draws,
    const PreparedModelDrawRangeList& ranges) noexcept
{
    PreparedModelSurfaceMaterialBindingStats stats{};
    stats.surface_count = detail::prepared_model_draw_saturating_count(surfaces.size());

    for (const auto& surface : surfaces) {
        detail::prepared_model_surface_add_material_stat(stats.materials, surface.material_mode);

        bool bound = prepared_model_surface_material_mode_valid(surface.material_mode);
        if (surface.range_index >= ranges.ranges.size()) {
            detail::prepared_model_draw_add_saturating(stats.invalid_range_count, 1u);
            continue;
        }

        const auto& range = ranges.ranges[surface.range_index];
        const size_t range_begin = range.draw_begin;
        const size_t range_count = range.draw_count;
        if (range_begin > draws.draws.size()
            || range_count > draws.draws.size() - range_begin) {
            detail::prepared_model_draw_add_saturating(stats.range_mismatch_count, 1u);
            continue;
        }
        if (surface.draw_index >= draws.draws.size()) {
            detail::prepared_model_draw_add_saturating(stats.invalid_draw_index_count, 1u);
            continue;
        }
        if (surface.draw_index < range_begin || surface.draw_index >= range_begin + range_count) {
            detail::prepared_model_draw_add_saturating(stats.range_mismatch_count, 1u);
            continue;
        }

        const auto& draw = draws.draws[surface.draw_index];
        if (!detail::prepared_model_draw_matches_range(range, draw)
            || !detail::prepared_model_surface_matches_range(surface, range)
            || surface.source_draw_index != draw.source_draw_index) {
            detail::prepared_model_draw_add_saturating(stats.payload_mismatch_count, 1u);
            bound = false;
        }

        if (surface.material_index != draw.material_index
            || surface.material_override != draw.material_override
            || surface.skin_index != draw.skin_index
            || surface.material_mode != draw.material_mode
            || surface.material_uses_fallback != draw.material_uses_fallback
            || surface.material_payload != draw.material_payload
            || surface.skinned != draw.skinned) {
            detail::prepared_model_draw_add_saturating(stats.material_mismatch_count, 1u);
            bound = false;
        }

        const bool expected_casts_shadow = draw.instance_casts_shadow
            && draw.primitive_casts_shadow
            && prepared_model_surface_casts_shadow(draw.material_mode);
        if (surface.casts_shadow != expected_casts_shadow) {
            detail::prepared_model_draw_add_saturating(stats.shadow_mismatch_count, 1u);
            bound = false;
        }

        if (bound) {
            if (surface.material_payload == PreparedModelMaterialPayloadKind::fallback) {
                detail::prepared_model_draw_add_saturating(stats.fallback_material_payload_count, 1u);
                detail::prepared_model_draw_add_saturating(
                    stats.render_model_fallback_material_payload_count,
                    1u);
            }
            if (surface.material_uses_fallback) {
                detail::prepared_model_draw_add_saturating(stats.material_fallback_count, 1u);
                detail::prepared_model_draw_add_saturating(
                    stats.render_model_material_fallback_count,
                    1u);
            }
            if (surface.material_override.valid()) {
                detail::prepared_model_draw_add_saturating(stats.material_override_surface_count, 1u);
            }
            detail::prepared_model_draw_add_saturating(stats.bound_surface_count, 1u);
        }
    }

    return stats;
}

[[nodiscard]] inline uint32_t prepared_model_surface_pass_index(MaterialMode mode) noexcept
{
    switch (mode) {
    case MaterialMode::opaque:
        return 0u;
    case MaterialMode::cutout:
        return 1u;
    case MaterialMode::water:
        return 2u;
    case MaterialMode::transparent:
        return 3u;
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] inline bool prepared_model_surface_sort_less(
    const PreparedModelSurfaceDraw& lhs,
    const PreparedModelSurfaceDraw& rhs) noexcept
{
    const uint32_t lhs_pass = prepared_model_surface_pass_index(lhs.material_mode);
    const uint32_t rhs_pass = prepared_model_surface_pass_index(rhs.material_mode);
    if (lhs_pass != rhs_pass) {
        return lhs_pass < rhs_pass;
    }
    if (lhs.skinned != rhs.skinned) {
        return !lhs.skinned && rhs.skinned;
    }
    if (lhs.instance_source_index != rhs.instance_source_index) {
        return lhs.instance_source_index < rhs.instance_source_index;
    }
    if (lhs.material_index != rhs.material_index) {
        return lhs.material_index < rhs.material_index;
    }
    if (lhs.material_override != rhs.material_override) {
        return lhs.material_override < rhs.material_override;
    }
    if (lhs.skin_index != rhs.skin_index) {
        return lhs.skin_index < rhs.skin_index;
    }
    if (lhs.range_index != rhs.range_index) {
        return lhs.range_index < rhs.range_index;
    }
    if (lhs.source_draw_index != rhs.source_draw_index) {
        return lhs.source_draw_index < rhs.source_draw_index;
    }
    return lhs.handle_index < rhs.handle_index;
}

inline void sort_prepared_model_surface_draws_by_pass(
    std::span<PreparedModelSurfaceDraw> draws,
    PreparedModelSurfaceSortCache& cache)
{
    if (draws.size() < 2u || draws.size() > std::numeric_limits<uint32_t>::max()) {
        cache.keys.clear();
        cache.order.clear();
        cache.permutation.clear();
        std::sort(draws.begin(), draws.end(), prepared_model_surface_sort_less);
        return;
    }

    bool sort_needed = cache.keys.size() != draws.size() || cache.order.size() != draws.size();
    if (sort_needed) {
        cache.order.clear();
        cache.keys.resize(draws.size());
    }
    for (size_t index = 0; index < draws.size(); ++index) {
        const auto& draw = draws[index];
        const PreparedModelSurfaceSortKey fields{
            prepared_model_surface_pass_index(draw.material_mode),
            static_cast<uint32_t>(draw.skinned),
            draw.instance_source_index,
            draw.material_index,
            draw.material_override.index,
            draw.material_override.generation,
            draw.skin_index,
            draw.range_index,
            draw.source_draw_index,
            draw.handle_index,
        };
        if (fields != cache.keys[index]) {
            cache.keys[index] = fields;
            sort_needed = true;
        }
    }
    if (sort_needed) {
        // Invalidate before allocating so an allocation failure cannot leave
        // an old permutation paired with the freshly collected keys.
        cache.order.clear();
        cache.order.reserve(draws.size());
        for (size_t index = 0; index < draws.size(); ++index) {
            cache.order.push_back(static_cast<uint32_t>(index));
        }
        std::sort(cache.order.begin(), cache.order.end(), [&](uint32_t lhs, uint32_t rhs) {
            return cache.keys[lhs] < cache.keys[rhs];
        });
    }

    cache.permutation = cache.order;
    // Move each cycle once, marking completed positions with the identity
    // permutation. The retained keys/order still describe the unsorted input.
    for (size_t first = 0; first < draws.size(); ++first) {
        if (cache.permutation[first] == first) {
            continue;
        }
        auto saved = std::move(draws[first]);
        size_t destination = first;
        while (cache.permutation[destination] != first) {
            const size_t source = cache.permutation[destination];
            draws[destination] = std::move(draws[source]);
            cache.permutation[destination] = static_cast<uint32_t>(destination);
            destination = source;
        }
        draws[destination] = std::move(saved);
        cache.permutation[destination] = static_cast<uint32_t>(destination);
    }
}

inline void sort_prepared_model_surface_draws_by_pass(std::span<PreparedModelSurfaceDraw> draws)
{
    PreparedModelSurfaceSortCache cache;
    sort_prepared_model_surface_draws_by_pass(draws, cache);
}

[[nodiscard]] inline size_t first_prepared_model_surface_pass_at_least(
    std::span<const PreparedModelSurfaceDraw> draws,
    uint32_t pass_index) noexcept
{
    const auto it = std::lower_bound(
        draws.begin(),
        draws.end(),
        pass_index,
        [](const PreparedModelSurfaceDraw& draw, uint32_t pass) noexcept {
            return prepared_model_surface_pass_index(draw.material_mode) < pass;
        });
    return static_cast<size_t>(it - draws.begin());
}

[[nodiscard]] inline std::span<const PreparedModelSurfaceDraw> prepared_model_surface_draws_for_pass_indices(
    std::span<const PreparedModelSurfaceDraw> draws,
    uint32_t first_pass_index,
    uint32_t one_past_last_pass_index) noexcept
{
    // Input contract: draws are already sorted by
    // sort_prepared_model_surface_draws_by_pass. This returns a view into the
    // caller-owned flat draw array; it does not allocate or copy draw records.
    if (first_pass_index >= one_past_last_pass_index || draws.empty()) {
        return {};
    }

    const size_t begin = first_prepared_model_surface_pass_at_least(draws, first_pass_index);
    const size_t end = first_prepared_model_surface_pass_at_least(draws, one_past_last_pass_index);
    if (begin >= end) {
        return {};
    }
    return std::span<const PreparedModelSurfaceDraw>{draws.data() + begin, end - begin};
}

[[nodiscard]] inline bool prepared_render_model_surface_matches_model(
    const RenderModel& model,
    const PreparedModelSurfaceDraw& surface) noexcept
{
    if (surface.instance_source_index == kInvalidPreparedModelDrawIndex
        || surface.source_draw_index >= model.primitives.size()) {
        return false;
    }

    const auto& primitive = model.primitives[surface.source_draw_index];
    if (primitive.material >= model.materials.size()
        || surface.material_index != primitive.material
        || surface.skin_index != primitive.skin
        || surface.skinned != primitive.skinned) {
        return false;
    }

    const auto& material = model.materials[primitive.material];
    return surface.material_mode == material.alpha_mode
        && surface.material_uses_fallback == material.material_uses_fallback
        && surface.material_payload == prepared_render_model_material_payload_kind(material);
}

[[nodiscard]] inline bool prepared_render_model_shadow_surface_matches_model(
    const RenderModel& model,
    const PreparedModelSurfaceDraw& surface) noexcept
{
    return surface.casts_shadow
        && prepared_model_surface_casts_shadow(surface.material_mode)
        && prepared_render_model_surface_matches_model(model, surface);
}

// Linear renderer-facing partition over the flat surface stream. The caller owns
// pass filtering and source asset validation; this helper groups contiguous
// surfaces by the stable scene source index.
[[nodiscard]] inline PreparedModelSurfaceRun next_prepared_render_model_surface_run(
    std::span<const PreparedModelSurfaceDraw> surfaces,
    size_t cursor) noexcept
{
    if (cursor >= surfaces.size()) {
        return PreparedModelSurfaceRun{
            .draws = {},
            .instance_source_index = kInvalidPreparedModelDrawIndex,
            .next = surfaces.size(),
        };
    }

    const size_t begin = cursor;
    const uint32_t source_index = surfaces[begin].instance_source_index;
    size_t end = begin + 1u;
    while (end < surfaces.size()
        && surfaces[end].instance_source_index == source_index) {
        ++end;
    }

    return PreparedModelSurfaceRun{
        .draws = std::span<const PreparedModelSurfaceDraw>{surfaces.data() + begin, end - begin},
        .instance_source_index = source_index,
        .next = end,
    };
}

inline void collect_prepared_render_model_surface_runs(
    PreparedRenderModelSurfaceRunList& out,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    size_t render_model_source_count)
{
    out.clear();
    out.stats.input_surface_count = detail::prepared_model_draw_saturating_count(surfaces.size());
    out.runs.reserve(surfaces.size());

    for (size_t cursor = 0; cursor < surfaces.size();) {
        const auto& surface = surfaces[cursor];
        const size_t begin = cursor;
        const uint32_t source_index = surface.instance_source_index;
        ++cursor;
        while (cursor < surfaces.size()
            && surfaces[cursor].instance_source_index == source_index) {
            ++cursor;
        }

        const uint32_t run_surface_count = detail::prepared_model_draw_saturating_count(cursor - begin);
        detail::prepared_model_draw_add_saturating(
            out.stats.render_model_surface_count,
            run_surface_count);

        if (source_index == kInvalidPreparedModelDrawIndex
            || static_cast<size_t>(source_index) >= render_model_source_count) {
            detail::prepared_model_draw_add_saturating(
                out.stats.invalid_source_index_count,
                run_surface_count);
            continue;
        }

        out.runs.push_back(PreparedModelSurfaceRun{
            .draws = std::span<const PreparedModelSurfaceDraw>{surfaces.data() + begin, cursor - begin},
            .instance_source_index = source_index,
            .next = cursor,
        });
        detail::prepared_model_draw_add_saturating(out.stats.run_count, 1u);
    }
}

inline void collect_prepared_render_model_skin_tables(
    PreparedRenderModelSkinTable& out,
    std::span<PreparedModelSurfaceDraw> surfaces,
    const ModelInstanceStore& instances)
{
    out.clear();
    out.stats.input_surface_count = detail::prepared_model_draw_saturating_count(surfaces.size());
    out.ranges.reserve(surfaces.size());

    const auto find_range = [&out](ModelInstanceHandle instance, uint32_t source_skin_index) noexcept {
        for (size_t i = out.ranges.size(); i > 0; --i) {
            const size_t range_index = i - 1u;
            const auto& range = out.ranges[range_index];
            if (range.instance == instance && range.source_skin_index == source_skin_index) {
                return detail::prepared_model_draw_saturating_count(range_index);
            }
        }
        return kInvalidPreparedModelDrawIndex;
    };

    for (auto& surface : surfaces) {
        surface.skin_table_index = kInvalidPreparedModelDrawIndex;

        if (!surface.skinned) {
            detail::prepared_model_draw_add_saturating(out.stats.unskinned_surface_count, 1u);
            continue;
        }

        detail::prepared_model_draw_add_saturating(out.stats.render_model_skinned_surface_count, 1u);
        if (surface.skin_index == kInvalidPreparedModelDrawIndex) {
            detail::prepared_model_draw_add_saturating(out.stats.invalid_skin_index_count, 1u);
            continue;
        }

        const auto* instance = instances.get(surface.instance);
        if (!instance) {
            detail::prepared_model_draw_add_saturating(out.stats.stale_instance_count, 1u);
            continue;
        }
        if (surface.skin_index >= instance->animation.skin_matrices.size()) {
            detail::prepared_model_draw_add_saturating(out.stats.bind_pose_fallback_surface_count, 1u);
            continue;
        }

        const uint32_t existing_range = find_range(surface.instance, surface.skin_index);
        if (existing_range != kInvalidPreparedModelDrawIndex) {
            surface.skin_table_index = existing_range;
            detail::prepared_model_draw_add_saturating(out.stats.assigned_surface_count, 1u);
            detail::prepared_model_draw_add_saturating(out.stats.reused_surface_count, 1u);
            continue;
        }

        const auto& source_matrices = instance->animation.skin_matrices[surface.skin_index];
        if (out.ranges.size() > std::numeric_limits<uint32_t>::max()
            || out.matrices.size() > std::numeric_limits<uint32_t>::max()
            || !model_skin_bone_count_supported(source_matrices.size())
            || source_matrices.size() > std::numeric_limits<uint32_t>::max()
            || source_matrices.size() > std::numeric_limits<uint32_t>::max() - out.matrices.size()) {
            detail::prepared_model_draw_add_saturating(out.stats.invalid_matrix_range_count, 1u);
            continue;
        }

        const uint32_t range_index = static_cast<uint32_t>(out.ranges.size());
        const uint32_t matrix_begin = static_cast<uint32_t>(out.matrices.size());
        const uint32_t matrix_count = static_cast<uint32_t>(source_matrices.size());
        out.matrices.insert(out.matrices.end(), source_matrices.begin(), source_matrices.end());
        out.ranges.push_back(PreparedRenderModelSkinRange{
            .instance = surface.instance,
            .source_skin_index = surface.skin_index,
            .matrix_begin = matrix_begin,
            .matrix_count = matrix_count,
        });
        surface.skin_table_index = range_index;
        detail::prepared_model_draw_add_saturating(out.stats.assigned_surface_count, 1u);
        detail::prepared_model_draw_add_saturating(out.stats.table_entry_count, 1u);
        detail::prepared_model_draw_add_saturating(out.stats.matrix_count, matrix_count);
    }
}

inline void collect_prepared_model_surface_draws(
    PreparedModelSurfaceDrawList& out,
    const PreparedModelDrawList& list,
    const PreparedModelDrawRangeList& ranges)
{
    out.clear();
    const size_t reserve_count = ranges.stats.draw_count < list.draws.size()
        ? ranges.stats.draw_count
        : list.draws.size();
    out.draws.reserve(reserve_count);

    for (size_t range_index = 0; range_index < ranges.ranges.size(); ++range_index) {
        const auto& range = ranges.ranges[range_index];
        ++out.stats.range_count;
        if (range_index > std::numeric_limits<uint32_t>::max()) {
            ++out.stats.invalid_range_count;
            continue;
        }

        const auto draws = prepared_model_draws_for_range(list, range);
        if (draws.size() != range.draw_count) {
            ++out.stats.invalid_range_count;
            continue;
        }

        for (size_t draw_offset = 0; draw_offset < draws.size(); ++draw_offset) {
            const auto& draw = draws[draw_offset];
            if (!detail::prepared_model_draw_matches_range(range, draw)) {
                ++out.stats.range_mismatch_count;
                continue;
            }

            const size_t source_draw_list_index = static_cast<size_t>(range.draw_begin) + draw_offset;
            const bool casts_shadow = draw.instance_casts_shadow
                && draw.primitive_casts_shadow
                && prepared_model_surface_casts_shadow(draw.material_mode);
            out.draws.push_back(PreparedModelSurfaceDraw{
                .instance = range.instance,
                .range_index = static_cast<uint32_t>(range_index),
                .draw_index = detail::prepared_model_draw_saturating_count(source_draw_list_index),
                .handle_index = range.handle_index,
                .instance_source_index = range.instance_source_index,
                .source_draw_index = draw.source_draw_index,
                .material_index = draw.material_index,
                .material_override = draw.material_override,
                .skin_index = draw.skin_index,
                .skin_table_index = kInvalidPreparedModelDrawIndex,
                .material_mode = draw.material_mode,
                .material_uses_fallback = draw.material_uses_fallback,
                .material_payload = draw.material_payload,
                .skinned = draw.skinned,
                .casts_shadow = casts_shadow,
                .winding = model_transform_winding(draw.world),
                .world = draw.world,
                .normal_matrix = draw.normal_matrix,
                .bounds = draw.bounds,
            });
            ++out.stats.draw_count;
            if (casts_shadow) {
                ++out.stats.shadow_caster_draw_count;
            }
            ++out.stats.render_model_draw_count;

            // Range identity was checked above; every binding field is copied
            // from that range/draw. Count the emitted bindings while they are
            // hot instead of reading all three arrays again after collection.
            auto& bindings = out.material_bindings;
            detail::prepared_model_surface_add_material_stat(bindings.materials, draw.material_mode);
            if (!prepared_model_surface_material_mode_valid(draw.material_mode)) {
                continue;
            }
            detail::prepared_model_draw_add_saturating(bindings.bound_surface_count, 1u);
            if (draw.material_payload == PreparedModelMaterialPayloadKind::fallback) {
                detail::prepared_model_draw_add_saturating(bindings.fallback_material_payload_count, 1u);
                detail::prepared_model_draw_add_saturating(bindings.render_model_fallback_material_payload_count, 1u);
            }
            if (draw.material_uses_fallback) {
                detail::prepared_model_draw_add_saturating(bindings.material_fallback_count, 1u);
                detail::prepared_model_draw_add_saturating(bindings.render_model_material_fallback_count, 1u);
            }
            if (draw.material_override.valid()) {
                detail::prepared_model_draw_add_saturating(bindings.material_override_surface_count, 1u);
            }
        }
    }
    out.material_bindings.surface_count = detail::prepared_model_draw_saturating_count(out.draws.size());
    if (list.draws.size() > std::numeric_limits<uint32_t>::max()) {
        // Oversized source indices are saturated in the surface protocol.
        // Preserve validation of those indices rather than assuming parity.
        out.material_bindings = validate_prepared_model_surface_material_bindings(out.draws, list, ranges);
    }
}

inline void collect_prepared_model_surface_draws(
    PreparedModelSurfaceDrawList& out,
    const PreparedModelDrawList& list,
    const PreparedModelDrawRangeList& ranges,
    const ModelInstanceStore& instances)
{
    collect_prepared_model_surface_draws(out, list, ranges);
    collect_prepared_render_model_skin_tables(
        out.render_model_skins,
        std::span<PreparedModelSurfaceDraw>{out.draws.data(), out.draws.size()},
        instances);
}

} // namespace nw::render
