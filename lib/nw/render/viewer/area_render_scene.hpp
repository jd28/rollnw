#pragma once

#include <nw/render/model.hpp>
#include <nw/render/model_draw.hpp>
#include <nw/render/model_render_context.hpp>
#include <nw/render/nwn/model_renderer.hpp>
#include <nw/render/render_context.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace nw::gfx {
struct CommandList;
} // namespace nw::gfx

namespace nw::render {
struct RenderService;
} // namespace nw::render

namespace nw::render::viewer {

struct PreviewScene;

inline constexpr uint32_t kInvalidAreaRenderRecordIndex = std::numeric_limits<uint32_t>::max();
inline constexpr float kAreaRenderTileSize = 10.0f;

enum class AreaRenderRecordKind : uint8_t {
    unknown,
    tile,
    creature,
    door,
    item,
    placeable,
};

struct AreaRenderSourceInfo {
    AreaRenderRecordKind kind = AreaRenderRecordKind::unknown;
    int16_t tile_x = -1;
    int16_t tile_y = -1;
    uint8_t tile_orientation = 0;
    bool static_candidate = false;
};

struct AreaRenderSceneStats {
    uint32_t record_count = 0;
    uint32_t static_record_count = 0;
    uint32_t dynamic_record_count = 0;
    uint32_t disabled_record_count = 0;
    uint32_t tile_record_count = 0;
    uint32_t creature_record_count = 0;
    uint32_t door_record_count = 0;
    uint32_t item_record_count = 0;
    uint32_t placeable_record_count = 0;
    uint32_t unknown_record_count = 0;
    uint32_t opaque_cutout_record_count = 0;
    uint32_t water_record_count = 0;
    uint32_t transparent_record_count = 0;
    uint32_t shadow_caster_record_count = 0;
    uint32_t prepared_draw_count = 0;
    uint32_t shadow_prepared_surface_count = 0;
    uint32_t static_geometry_mesh_count = 0;
    uint32_t static_geometry_vertex_count = 0;
    uint32_t static_geometry_index_count = 0;
    uint32_t static_geometry_bytes = 0;
    uint32_t max_prepared_draws_per_record = 0;
    uint32_t max_shadow_prepared_surfaces_per_record = 0;
    uint32_t light_index_count = 0;
    uint32_t local_light_count = 0;
    uint32_t dynamic_light_count = 0;
    uint32_t max_light_indices_per_record = 0;
    uint32_t chunk_light_index_count = 0;
    uint32_t max_light_indices_per_chunk = 0;
    uint32_t chunk_count = 0;
    uint32_t nonempty_chunk_count = 0;
    uint32_t max_records_per_chunk = 0;
    uint32_t chunk_width = 0;
    uint32_t chunk_height = 0;
};

// Area prepared-surface to NWN sidecar bridge stats. Inputs are caller-owned
// common surface indices plus stable sidecar rows; output pointers are
// frame/scratch-local views into that sidecar span. Invalid indices, non-NWN
// surfaces, missing sidecar rows, and mismatched payloads are dropped here.
struct AreaPreparedSurfaceSidecarStats {
    uint32_t input_surface_count = 0;
    uint32_t selected_draw_count = 0;
    uint32_t dropped_non_nwn_surface_count = 0;
    uint32_t dropped_invalid_surface_index_count = 0;
    uint32_t dropped_missing_sidecar_draw_count = 0;
    uint32_t dropped_invalid_sidecar_draw_count = 0;

    [[nodiscard]] uint32_t dropped_surface_count() const noexcept
    {
        const uint64_t total = static_cast<uint64_t>(dropped_non_nwn_surface_count)
            + static_cast<uint64_t>(dropped_invalid_surface_index_count)
            + static_cast<uint64_t>(dropped_missing_sidecar_draw_count)
            + static_cast<uint64_t>(dropped_invalid_sidecar_draw_count);
        return total > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(total);
    }

    void add(const AreaPreparedSurfaceSidecarStats& source) noexcept
    {
        const auto add_saturating = [](uint32_t& target, uint32_t value) noexcept {
            const uint64_t next = static_cast<uint64_t>(target) + static_cast<uint64_t>(value);
            target = static_cast<uint32_t>(std::min<uint64_t>(next, std::numeric_limits<uint32_t>::max()));
        };

        add_saturating(input_surface_count, source.input_surface_count);
        add_saturating(selected_draw_count, source.selected_draw_count);
        add_saturating(dropped_non_nwn_surface_count, source.dropped_non_nwn_surface_count);
        add_saturating(dropped_invalid_surface_index_count, source.dropped_invalid_surface_index_count);
        add_saturating(dropped_missing_sidecar_draw_count, source.dropped_missing_sidecar_draw_count);
        add_saturating(dropped_invalid_sidecar_draw_count, source.dropped_invalid_sidecar_draw_count);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return dropped_surface_count() == 0;
    }
};

struct AreaRenderFrameStats {
    uint32_t visible_record_count = 0;
    uint32_t visible_static_record_count = 0;
    uint32_t visible_dynamic_record_count = 0;
    uint32_t visible_chunk_count = 0;
    uint32_t culled_record_count = 0;
    uint32_t culled_chunk_count = 0;
    uint32_t visibility_culled_record_count = 0;
    uint32_t visibility_culled_chunk_count = 0;
    uint32_t opaque_cutout_record_count = 0;
    uint32_t water_record_count = 0;
    uint32_t transparent_record_count = 0;
    uint32_t shadow_caster_record_count = 0;
    uint32_t visible_prepared_surface_count = 0;
    uint32_t visible_light_count = 0;
    AreaPreparedSurfaceSidecarStats material_indirect_sidecar_bridge{};
    AreaPreparedSurfaceSidecarStats static_material_indirect_sidecar_bridge{};
    bool uses_cached_draw_lists = false;
    bool uses_sorted_static_draw_lists = false;
};

struct AreaRenderCullContext {
    glm::mat4 view_projection{1.0f};
    std::span<const uint8_t> visible_chunk_mask;
    bool enabled = false;
    bool chunk_visibility_enabled = false;
    bool collect_direct_render_model_records = true;
};

enum class AreaVisibilityMaskMode : uint8_t {
    radius,
    view_cone,
};

struct AreaVisibilityMaskOptions {
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_target{0.0f, 1.0f, 0.0f};
    int32_t radius_tiles = -1;
    float half_angle_degrees = 75.0f;
    AreaVisibilityMaskMode mode = AreaVisibilityMaskMode::radius;
};

struct AreaPreparedIndirectDrawCache {
    std::vector<nw::gfx::IndexedIndirectDrawCommand> commands;
    nwn::PreparedIndirectDrawCommands indirect;
    uint64_t signature = 0;
    uint32_t source_index_count = std::numeric_limits<uint32_t>::max();

    AreaPreparedIndirectDrawCache() = default;
    AreaPreparedIndirectDrawCache(const AreaPreparedIndirectDrawCache&) = delete;
    AreaPreparedIndirectDrawCache& operator=(const AreaPreparedIndirectDrawCache&) = delete;

    void clear()
    {
        commands.clear();
        indirect = {};
        signature = 0;
        source_index_count = std::numeric_limits<uint32_t>::max();
    }

    void clear_gpu_commands() noexcept
    {
        indirect.gpu_commands = {};
    }
};

struct AreaStaticMaterialDrawBatch {
    // Bridge contract for area static sidecar submission. `surface_indices` is
    // the common surface stream used by material/depth submission; legacy NWN
    // draw pointers are materialized from those surfaces only at the renderer
    // boundary.
    std::span<const uint32_t> surface_indices;
    nw::render::MaterialMode material = nw::render::MaterialMode::opaque;
    bool static_geometry_sorted = false;
    nwn::PreparedIndirectDrawCommands indirect_draws;
    nw::gfx::StorageSpan cached_draw_data;
};

struct AreaStaticMaterialDrawBatches {
    std::array<AreaStaticMaterialDrawBatch, 2> batches;
    uint32_t count = 0;

    [[nodiscard]] std::span<const AreaStaticMaterialDrawBatch> span() const noexcept
    {
        return {batches.data(), count};
    }
};

struct AreaShadowCascadeDraws {
    static constexpr uint64_t kPreparedDrawSignatureOffset = 1469598103934665603ull;
    static constexpr uint64_t kPreparedDrawSignaturePrime = 1099511628211ull;

    std::vector<uint32_t> record_indices;
    std::vector<uint32_t> fallback_record_indices;
    std::vector<uint32_t> prepared_surface_indices;
    std::vector<uint32_t> record_marks;
    AreaPreparedIndirectDrawCache prepared_indirect;
    nwn::PreparedStaticShadowDrawDataCache prepared_draw_data;
    uint64_t prepared_surface_signature = kPreparedDrawSignatureOffset;
    uint32_t prepared_surface_signature_count = 0;
    uint32_t mark_generation = 1;

    void clear()
    {
        record_indices.clear();
        fallback_record_indices.clear();
        prepared_surface_indices.clear();
        reset_prepared_surface_signature();
        if (mark_generation == std::numeric_limits<uint32_t>::max()) {
            std::fill(record_marks.begin(), record_marks.end(), 0u);
            mark_generation = 1;
        }
        ++mark_generation;
    }

    void clear_cache()
    {
        prepared_indirect.clear();
        prepared_draw_data.clear();
    }

    void reserve(size_t record_count, size_t prepared_surface_count)
    {
        record_indices.reserve(record_count);
        fallback_record_indices.reserve(record_count);
        prepared_surface_indices.reserve(prepared_surface_count);
        record_marks.resize(record_count, 0u);
    }

    void mark_record(uint32_t record_index) noexcept
    {
        if (record_index < record_marks.size()) {
            record_marks[record_index] = mark_generation;
        }
    }

    [[nodiscard]] bool record_marked(uint32_t record_index) const noexcept
    {
        return record_index < record_marks.size() && record_marks[record_index] == mark_generation;
    }

    void append_prepared_surface(uint32_t surface_index)
    {
        prepared_surface_indices.push_back(surface_index);
        mix_prepared_surface_signature(surface_index);
        if (prepared_surface_signature_count != std::numeric_limits<uint32_t>::max()) {
            ++prepared_surface_signature_count;
        }
    }

    [[nodiscard]] uint64_t finalized_prepared_surface_signature(size_t prepared_surface_count) const noexcept
    {
        uint64_t hash = prepared_surface_signature;
        hash ^= prepared_surface_count;
        hash *= kPreparedDrawSignaturePrime;
        hash ^= prepared_surface_signature_count;
        hash *= kPreparedDrawSignaturePrime;
        return hash;
    }

    [[nodiscard]] nwn::PreparedIndirectDrawCommands prepared_indirect_draws() const noexcept
    {
        return prepared_indirect.indirect;
    }

    [[nodiscard]] nwn::PreparedStaticShadowDrawDataCache& prepared_draw_data_cache() noexcept
    {
        return prepared_draw_data;
    }

    [[nodiscard]] nw::gfx::StorageSpan prepared_draw_data_span() const noexcept
    {
        return prepared_draw_data.span;
    }

private:
    void reset_prepared_surface_signature() noexcept
    {
        prepared_surface_signature = kPreparedDrawSignatureOffset;
        prepared_surface_signature_count = 0;
    }

    void mix_prepared_surface_signature(uint64_t value) noexcept
    {
        prepared_surface_signature ^= value;
        prepared_surface_signature *= kPreparedDrawSignaturePrime;
    }
};

class AreaRenderScene {
public:
    enum RecordFlag : uint8_t {
        render_enabled = 1u << 0u,
        static_candidate = 1u << 1u,
        shadow_caster = 1u << 2u,
    };

    ~AreaRenderScene();

    void clear();
    void rebuild(const PreviewScene& scene, nw::gfx::Context* gfx = nullptr);
    // Batch refresh from common ModelInstance runtime state into area record
    // cache fields. Dynamic records copy common world bounds/root/shadow flags;
    // static prepared records keep rebuild-time bounds/root because their
    // prepared draw payload is cached. Stale or mismatched handles are dropped
    // from frame selection by clearing render/shadow flags.
    void refresh_runtime_records(const PreviewScene& scene);
    void refresh_light_indices(const PreviewScene& scene);

    [[nodiscard]] bool empty() const noexcept { return model_indices_.empty(); }
    [[nodiscard]] const AreaRenderSceneStats& stats() const noexcept { return stats_; }
    [[nodiscard]] bool has_scene_bounds() const noexcept { return has_scene_bounds_; }
    [[nodiscard]] const nw::render::Bounds& scene_bounds() const noexcept { return scene_bounds_; }
    [[nodiscard]] std::span<const uint32_t> model_indices() const noexcept { return model_indices_; }
    [[nodiscard]] uint32_t record_index_for_model(uint32_t model_index) const noexcept
    {
        return model_index < model_record_indices_.size()
            ? model_record_indices_[model_index]
            : kInvalidAreaRenderRecordIndex;
    }
    [[nodiscard]] uint32_t record_index_for_render_model(uint32_t model_index) const noexcept
    {
        return model_index < render_model_record_indices_.size()
            ? render_model_record_indices_[model_index]
            : kInvalidAreaRenderRecordIndex;
    }
    [[nodiscard]] std::span<const nw::render::ModelInstanceKind> model_kinds() const noexcept
    {
        return model_kinds_;
    }
    [[nodiscard]] std::span<const nwn::ModelInstance* const> models() const noexcept { return models_; }
    [[nodiscard]] std::span<const nw::render::ModelInstanceHandle> model_instance_handles() const noexcept
    {
        return model_instance_handles_;
    }
    [[nodiscard]] std::span<const nw::render::Bounds> bounds() const noexcept { return bounds_; }
    [[nodiscard]] std::span<const glm::mat4> root_transforms() const noexcept { return root_transforms_; }
    [[nodiscard]] std::span<const uint8_t> pass_masks() const noexcept { return pass_masks_; }
    [[nodiscard]] std::span<const uint8_t> flags() const noexcept { return flags_; }
    [[nodiscard]] std::span<const uint32_t> chunk_ids() const noexcept { return chunk_ids_; }
    [[nodiscard]] std::span<const AreaRenderRecordKind> kinds() const noexcept { return kinds_; }
    [[nodiscard]] std::span<const int16_t> tile_xs() const noexcept { return tile_xs_; }
    [[nodiscard]] std::span<const int16_t> tile_ys() const noexcept { return tile_ys_; }
    [[nodiscard]] std::span<const uint32_t> chunk_offsets() const noexcept { return chunk_offsets_; }
    [[nodiscard]] std::span<const uint32_t> chunk_record_indices() const noexcept { return chunk_record_indices_; }
    [[nodiscard]] std::span<const nw::render::Bounds> chunk_bounds() const noexcept { return chunk_bounds_; }
    [[nodiscard]] std::span<const uint8_t> chunk_has_bounds() const noexcept { return chunk_has_bounds_; }
    [[nodiscard]] std::span<const uint32_t> prepared_draw_offsets() const noexcept { return prepared_draw_offsets_; }
    [[nodiscard]] std::span<const nwn::PreparedDrawItem> prepared_draws() const noexcept { return prepared_draws_; }
    // Bridge-phase area cache protocol: area records own the stable input
    // index, common records identify the selected draw, and the NWN prepared
    // draw sidecar still owns the legacy render payload. Dynamic, stale, and
    // out-of-range records expose empty spans.
    [[nodiscard]] const nw::render::PreparedModelDrawList& prepared_model_draw_list() const noexcept
    {
        return prepared_model_draws_;
    }
    [[nodiscard]] const nw::render::PreparedModelDrawRangeList& prepared_model_draw_ranges() const noexcept
    {
        return prepared_model_draw_ranges_;
    }
    [[nodiscard]] const nw::render::PreparedModelSurfaceDrawList& prepared_model_surface_draws() const noexcept
    {
        return prepared_model_surface_draws_;
    }
    [[nodiscard]] std::span<const nw::render::PreparedModelDraw> prepared_model_draws_for_record(
        uint32_t record_index) const noexcept;
    [[nodiscard]] std::span<const nw::render::PreparedModelSurfaceDraw> prepared_model_surface_draws_for_record(
        uint32_t record_index) const noexcept;
    // Common surface index protocol: the surface records remain owned by
    // prepared_model_surface_draws_. These sorted uint32_t indices provide a
    // renderer-facing pass stream without disturbing record-grouped offsets.
    [[nodiscard]] std::span<const uint32_t> prepared_surface_indices() const noexcept
    {
        return prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> opaque_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> cutout_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> opaque_cutout_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> water_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> transparent_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> opaque_static_prepared_surface_indices() const noexcept
    {
        return opaque_static_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> cutout_static_prepared_surface_indices() const noexcept
    {
        return cutout_static_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> prepared_draw_record_indices() const noexcept
    {
        return prepared_draw_record_indices_;
    }
    [[nodiscard]] std::span<const nwn::PreparedDrawItem> prepared_draws_for_record(uint32_t record_index) const noexcept;
    [[nodiscard]] std::span<const uint32_t> shadow_prepared_surface_indices() const noexcept
    {
        return shadow_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> sorted_shadow_prepared_surface_indices() const noexcept
    {
        return sorted_shadow_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> shadow_prepared_surface_indices_for_record(
        uint32_t record_index) const noexcept;
    [[nodiscard]] std::span<const uint32_t> static_material_prepared_surface_indices() const noexcept
    {
        return static_material_prepared_surface_indices_;
    }
    [[nodiscard]] uint64_t static_material_prepared_surface_signature() const noexcept
    {
        return static_material_prepared_surface_signature_;
    }
    [[nodiscard]] nwn::PreparedStaticDrawDataCache& static_material_draw_data_cache() noexcept
    {
        return static_material_draw_data_;
    }
    [[nodiscard]] nw::gfx::StorageSpan static_material_draw_data_span() const noexcept
    {
        return static_material_draw_data_.span;
    }
    [[nodiscard]] std::span<const uint32_t> light_indices() const noexcept { return light_indices_; }
    [[nodiscard]] std::span<const uint32_t> dynamic_light_indices() const noexcept { return dynamic_light_indices_; }
    [[nodiscard]] std::span<const uint32_t> light_indices_for_record(uint32_t record_index) const noexcept;
    [[nodiscard]] std::span<const uint32_t> chunk_light_indices() const noexcept { return chunk_light_indices_; }
    [[nodiscard]] std::span<const uint32_t> light_indices_for_chunk(uint32_t chunk_id) const noexcept;

private:
    std::vector<uint32_t> model_indices_;
    std::vector<uint32_t> model_record_indices_;
    std::vector<uint32_t> render_model_record_indices_;
    std::vector<nw::render::ModelInstanceKind> model_kinds_;
    std::vector<const nwn::ModelInstance*> models_;
    std::vector<nw::render::ModelInstanceHandle> model_instance_handles_;
    std::vector<nw::render::Bounds> bounds_;
    std::vector<glm::mat4> root_transforms_;
    std::vector<uint8_t> pass_masks_;
    std::vector<uint8_t> flags_;
    std::vector<uint32_t> chunk_ids_;
    std::vector<AreaRenderRecordKind> kinds_;
    std::vector<int16_t> tile_xs_;
    std::vector<int16_t> tile_ys_;
    std::vector<nw::render::Bounds> chunk_bounds_;
    std::vector<uint8_t> chunk_has_bounds_;
    std::vector<uint32_t> chunk_offsets_;
    std::vector<uint32_t> chunk_record_indices_;
    std::vector<uint32_t> prepared_draw_offsets_;
    std::vector<nwn::PreparedDrawItem> prepared_draws_;
    nw::render::PreparedModelDrawList prepared_model_draws_;
    nw::render::PreparedModelDrawRangeList prepared_model_draw_ranges_;
    nw::render::PreparedModelSurfaceDrawList prepared_model_surface_draws_;
    std::vector<uint32_t> prepared_surface_offsets_;
    std::vector<uint32_t> prepared_surface_indices_;
    std::array<uint32_t, 5> prepared_surface_pass_offsets_{};
    std::vector<uint32_t> opaque_static_prepared_surface_indices_;
    std::vector<uint32_t> cutout_static_prepared_surface_indices_;
    std::vector<uint32_t> prepared_draw_record_indices_;
    std::vector<uint32_t> shadow_prepared_surface_offsets_;
    std::vector<uint32_t> shadow_prepared_surface_indices_;
    std::vector<uint32_t> sorted_shadow_prepared_surface_indices_;
    std::vector<uint32_t> static_material_prepared_surface_indices_;
    std::vector<uint32_t> light_index_offsets_;
    std::vector<uint32_t> light_indices_;
    std::vector<uint32_t> dynamic_light_indices_;
    std::vector<uint32_t> chunk_light_index_offsets_;
    std::vector<uint32_t> chunk_light_indices_;
    nw::gfx::Handle<nw::gfx::Buffer> static_geometry_vertices_;
    nw::gfx::Handle<nw::gfx::Buffer> static_geometry_indices_;
    nwn::PreparedStaticDrawDataCache static_material_draw_data_;
    nw::render::Bounds scene_bounds_{};
    AreaRenderSceneStats stats_{};
    uint64_t static_material_prepared_surface_signature_ = 0;
    bool has_scene_bounds_ = false;

    bool build_static_geometry(nw::gfx::Context* gfx);
};

struct AreaDirectModelRecord {
    nw::render::ModelInstanceKind kind = nw::render::ModelInstanceKind::nwn_legacy;
    uint32_t record_index = kInvalidAreaRenderRecordIndex;
};

struct AreaDirectModelSubmissionStats {
    uint32_t input_record_count = 0;
    uint32_t selected_legacy_record_count = 0;
    uint32_t selected_render_model_record_count = 0;
    uint32_t skipped_render_model_record_count = 0;
    uint32_t dropped_invalid_record_count = 0;
    uint32_t dropped_invalid_source_count = 0;
    uint32_t dropped_unsupported_kind_count = 0;

    [[nodiscard]] uint32_t dropped_record_count() const noexcept
    {
        const uint64_t total = static_cast<uint64_t>(dropped_invalid_record_count)
            + static_cast<uint64_t>(dropped_invalid_source_count)
            + static_cast<uint64_t>(dropped_unsupported_kind_count);
        return total > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(total);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return dropped_record_count() == 0;
    }
};

struct AreaDirectModelRecordSelection {
    std::vector<AreaDirectModelRecord> records;

    void clear()
    {
        records.clear();
    }

    void reserve(size_t count)
    {
        records.reserve(count);
    }
};

class AreaRenderFrame {
public:
    void clear();
    void reserve_for_scene(const AreaRenderScene& scene);

    [[nodiscard]] const AreaRenderFrameStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::span<const uint32_t> visible_record_indices() const noexcept { return visible_record_indices_; }
    [[nodiscard]] bool has_visible_bounds() const noexcept { return has_visible_bounds_; }
    [[nodiscard]] const nw::render::Bounds& visible_bounds() const noexcept { return visible_bounds_; }
    [[nodiscard]] bool has_shadow_caster_bounds() const noexcept { return has_shadow_caster_bounds_; }
    [[nodiscard]] const nw::render::Bounds& shadow_caster_bounds() const noexcept { return shadow_caster_bounds_; }
    [[nodiscard]] std::span<const uint32_t> opaque_cutout_record_indices() const noexcept
    {
        return opaque_cutout_record_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> water_record_indices() const noexcept { return water_record_indices_; }
    [[nodiscard]] std::span<const uint32_t> transparent_record_indices() const noexcept
    {
        return transparent_record_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> shadow_caster_record_indices() const noexcept
    {
        return shadow_caster_record_indices_;
    }
    [[nodiscard]] std::span<const nw::render::ModelInstanceHandle> visible_render_model_instance_handles() const noexcept
    {
        return visible_render_model_instance_handles_;
    }
    [[nodiscard]] std::span<const nw::render::ModelInstanceHandle> visible_nwn_legacy_instance_handles() const noexcept
    {
        return visible_nwn_legacy_instance_handles_;
    }
    [[nodiscard]] const AreaDirectModelRecordSelection& direct_model_records_for_pass(
        nw::render::RenderPassSelection pass) const noexcept;
    [[nodiscard]] std::span<const uint32_t> visible_light_indices() const noexcept
    {
        return visible_light_indices_;
    }
    [[nodiscard]] bool uses_filtered_light_indices() const noexcept { return filtered_light_indices_valid_; }
    [[nodiscard]] std::span<const uint32_t> visible_prepared_surface_indices() const noexcept
    {
        return cached_draw_scene_ ? cached_draw_scene_->prepared_surface_indices() : visible_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> visible_opaque_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> visible_cutout_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> visible_opaque_cutout_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> visible_water_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> visible_transparent_prepared_surface_indices() const noexcept;
    [[nodiscard]] std::span<const uint32_t> visible_opaque_static_prepared_surface_indices() const noexcept
    {
        return cached_draw_scene_ ? cached_draw_scene_->opaque_static_prepared_surface_indices()
                                  : visible_opaque_static_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<const uint32_t> visible_cutout_static_prepared_surface_indices() const noexcept
    {
        return cached_draw_scene_ ? cached_draw_scene_->cutout_static_prepared_surface_indices()
                                  : visible_cutout_static_prepared_surface_indices_;
    }
    [[nodiscard]] std::span<AreaShadowCascadeDraws> shadow_cascade_draws() noexcept
    {
        return shadow_cascade_draws_;
    }
    [[nodiscard]] bool uses_cached_draw_lists() const noexcept { return cached_draw_scene_ != nullptr; }
    [[nodiscard]] bool uses_sorted_static_draw_lists() const noexcept
    {
        return cached_draw_scene_ != nullptr || sorted_visible_static_draw_lists_;
    }
    [[nodiscard]] nwn::PreparedIndirectDrawCommands visible_opaque_prepared_indirect_draws() const noexcept
    {
        return sorted_visible_static_draw_lists_ ? visible_opaque_prepared_indirect_.indirect : nwn::PreparedIndirectDrawCommands{};
    }
    [[nodiscard]] nwn::PreparedIndirectDrawCommands visible_cutout_prepared_indirect_draws() const noexcept
    {
        return sorted_visible_static_draw_lists_ ? visible_cutout_prepared_indirect_.indirect : nwn::PreparedIndirectDrawCommands{};
    }
    [[nodiscard]] nwn::PreparedIndirectDrawCommands visible_opaque_static_material_indirect_draws() const noexcept
    {
        return sorted_visible_static_draw_lists_ ? visible_opaque_static_material_indirect_.indirect
                                                 : nwn::PreparedIndirectDrawCommands{};
    }
    [[nodiscard]] nwn::PreparedIndirectDrawCommands visible_cutout_static_material_indirect_draws() const noexcept
    {
        return sorted_visible_static_draw_lists_ ? visible_cutout_static_material_indirect_.indirect
                                                 : nwn::PreparedIndirectDrawCommands{};
    }
    [[nodiscard]] bool record_visible(uint32_t record_index) const noexcept
    {
        return record_index < record_marks_.size()
            && record_marks_[record_index] == record_mark_generation_;
    }

private:
    friend void prepare_area_frame(
        const AreaRenderScene& scene,
        AreaRenderFrame& frame,
        const AreaRenderCullContext& cull);

    std::vector<uint32_t> visible_record_indices_;
    std::vector<uint32_t> visible_chunk_indices_;
    std::vector<uint32_t> opaque_cutout_record_indices_;
    std::vector<uint32_t> water_record_indices_;
    std::vector<uint32_t> transparent_record_indices_;
    std::vector<uint32_t> shadow_caster_record_indices_;
    std::vector<nw::render::ModelInstanceHandle> visible_render_model_instance_handles_;
    std::vector<nw::render::ModelInstanceHandle> visible_nwn_legacy_instance_handles_;
    AreaDirectModelRecordSelection direct_all_records_;
    AreaDirectModelRecordSelection direct_opaque_cutout_records_;
    AreaDirectModelRecordSelection direct_water_records_;
    AreaDirectModelRecordSelection direct_transparent_records_;
    std::vector<uint32_t> visible_light_indices_;
    std::vector<uint32_t> visible_prepared_surface_indices_;
    std::array<uint32_t, 5> visible_prepared_surface_pass_offsets_{};
    std::vector<uint32_t> visible_opaque_static_prepared_surface_indices_;
    std::vector<uint32_t> visible_cutout_static_prepared_surface_indices_;
    AreaPreparedIndirectDrawCache visible_opaque_prepared_indirect_;
    AreaPreparedIndirectDrawCache visible_cutout_prepared_indirect_;
    AreaPreparedIndirectDrawCache visible_opaque_static_material_indirect_;
    AreaPreparedIndirectDrawCache visible_cutout_static_material_indirect_;
    std::array<AreaShadowCascadeDraws, nw::render::kShadowCascadeCount> shadow_cascade_draws_;
    std::vector<uint32_t> record_marks_;
    std::vector<uint32_t> chunk_marks_;
    std::vector<uint32_t> light_marks_;
    nw::render::Bounds visible_bounds_{};
    nw::render::Bounds shadow_caster_bounds_{};
    const AreaRenderScene* cached_draw_scene_ = nullptr;
    AreaRenderFrameStats stats_{};
    uint32_t record_mark_generation_ = 1;
    uint32_t mark_generation_ = 1;
    uint32_t light_mark_generation_ = 1;
    bool has_visible_bounds_ = false;
    bool has_shadow_caster_bounds_ = false;
    bool filtered_light_indices_valid_ = false;
    bool sorted_visible_static_draw_lists_ = false;
};

[[nodiscard]] std::string_view area_render_record_kind_label(AreaRenderRecordKind kind) noexcept;
[[nodiscard]] bool should_use_sorted_area_static_surface_lists(
    uint32_t visible_prepared_surface_count,
    uint32_t total_prepared_surface_count) noexcept;
[[nodiscard]] AreaStaticMaterialDrawBatches area_static_material_draw_batches_for_pass(
    const AreaRenderFrame& frame,
    nw::render::RenderPassSelection pass,
    nw::gfx::StorageSpan static_material_draw_data = {}) noexcept;
[[nodiscard]] AreaStaticMaterialDrawBatch area_static_material_depth_prepass_batch(
    const AreaRenderFrame& frame,
    nw::gfx::StorageSpan static_material_draw_data = {}) noexcept;
[[nodiscard]] nw::gfx::StorageSpan refresh_area_static_material_draw_data(
    nw::render::RenderService& render_service,
    AreaRenderScene& scene,
    const AreaRenderFrame& frame,
    nw::render::nwn::PreparedDrawScratch& scratch,
    AreaPreparedSurfaceSidecarStats* sidecar_stats = nullptr);
AreaPreparedSurfaceSidecarStats render_area_static_material_draw_batches(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& scene,
    std::span<const AreaStaticMaterialDrawBatch> batches,
    const nw::render::RenderContext& ctx,
    nw::render::nwn::PreparedDrawScratch& scratch);
bool render_area_static_material_depth_prepass(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& scene,
    const AreaStaticMaterialDrawBatch& batch,
    const nw::render::RenderContext& ctx,
    nw::render::nwn::PreparedDrawScratch& scratch,
    AreaPreparedSurfaceSidecarStats* sidecar_stats = nullptr);
size_t rebuild_area_visibility_mask(
    const AreaRenderScene& scene,
    const AreaVisibilityMaskOptions& options,
    std::vector<uint8_t>& mask);
AreaPreparedSurfaceSidecarStats refresh_prepared_shadow_indirect_cache(
    AreaShadowCascadeDraws& cascade_draws,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const nwn::PreparedDrawItem> draws);
// Batch bridge contract: reads surface_indices as indices into surfaces and
// writes out as non-owning pointers into sidecar_draws. sidecar_draws must
// outlive out; invalid rows are omitted and reported in the returned stats.
AreaPreparedSurfaceSidecarStats collect_area_prepared_surface_sidecar_draws(
    std::vector<const nwn::PreparedDrawItem*>& out,
    const AreaRenderScene& scene,
    std::span<const uint32_t> surface_indices);
AreaPreparedSurfaceSidecarStats collect_area_prepared_surface_sidecar_draws(
    std::vector<const nwn::PreparedDrawItem*>& out,
    std::span<const uint32_t> surface_indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const nwn::PreparedDrawItem> sidecar_draws);
void prepare_area_frame(
    const AreaRenderScene& scene,
    AreaRenderFrame& frame,
    const AreaRenderCullContext& cull = {});
enum class AreaDirectRenderModelRecordPolicy : uint8_t {
    ignore,
    count_skipped,
};

// Area direct fallback adapters. The selected frame-owned record stream is the
// input contract; invalid record/source rows are dropped and counted here before
// legacy sidecar or RenderModel draw calls can reach Vulkan.
AreaDirectModelSubmissionStats collect_area_direct_legacy_model_records(
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    AreaDirectRenderModelRecordPolicy render_model_policy = AreaDirectRenderModelRecordPolicy::ignore);
AreaDirectModelSubmissionStats render_area_direct_legacy_model_records(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    AreaDirectRenderModelRecordPolicy render_model_policy = AreaDirectRenderModelRecordPolicy::ignore);
AreaDirectModelSubmissionStats render_area_direct_render_model_records(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass);

} // namespace nw::render::viewer
