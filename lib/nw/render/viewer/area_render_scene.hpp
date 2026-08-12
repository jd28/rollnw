#pragma once

#include <nw/objects/ObjectHandle.hpp>
#include <nw/render/model.hpp>
#include <nw/render/model_draw.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nw::render::viewer {

struct PreviewScene;
class AreaRenderScene;

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
    nw::ObjectHandle object{};
    int16_t tile_x = -1;
    int16_t tile_y = -1;
    uint8_t tile_orientation = 0;
    bool static_candidate = false;
};

struct AreaObjectRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f};
};

struct AreaSurfaceTriangle {
    glm::vec3 v0{0.0f};
    glm::vec3 v1{0.0f};
    glm::vec3 v2{0.0f};
};

struct AreaSurfaceRange {
    nw::render::Bounds bounds{};
    uint32_t first_triangle = 0;
    uint32_t triangle_count = 0;
};

enum class AreaSurfaceHitStatus : uint8_t {
    hit,
    miss,
    invalid_input,
};

struct AreaSurfaceHit {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    float distance = 0.0f;
    uint32_t range_index = kInvalidAreaRenderRecordIndex;
    AreaSurfaceHitStatus status = AreaSurfaceHitStatus::invalid_input;
};

// Batch ray trace over prevalidated, upward-facing area surface triangles.
// Ranges partition contiguous triangle rows by stable tile model. Rays are
// normalized once per row; malformed columns or non-finite rays fail loudly.
void trace_area_surfaces(
    std::span<const AreaObjectRay> rays,
    std::span<const AreaSurfaceRange> ranges,
    std::span<const AreaSurfaceTriangle> triangles,
    std::span<AreaSurfaceHit> hits) noexcept;

[[nodiscard]] AreaSurfaceHit trace_area_surface(
    const AreaObjectRay& ray,
    std::span<const AreaSurfaceRange> ranges,
    std::span<const AreaSurfaceTriangle> triangles) noexcept;

enum class AreaObjectSelectionStatus : uint8_t {
    hit,
    miss,
    invalid_input,
};

enum class AreaObjectSelectionSource : uint8_t {
    none,
    area_record,
    debug_shape,
};

enum class AreaObjectSelectionTarget : uint8_t {
    object,
    tile,
};

struct AreaObjectSelectionOptions {
    AreaObjectSelectionTarget target = AreaObjectSelectionTarget::object;
    bool triggers_enabled = true;
    bool encounters_enabled = true;
};

struct AreaObjectSelection {
    // Indexes AreaRenderScene rows for area_record and
    // PreviewScene::debug_shape_selection_ranges for debug_shape.
    uint32_t record_index = kInvalidAreaRenderRecordIndex;
    nw::ObjectHandle object{};
    glm::vec3 position{0.0f};
    float distance = 0.0f;
    int16_t tile_x = -1;
    int16_t tile_y = -1;
    AreaRenderRecordKind kind = AreaRenderRecordKind::unknown;
    AreaObjectSelectionSource source = AreaObjectSelectionSource::none;
    AreaObjectSelectionStatus status = AreaObjectSelectionStatus::invalid_input;
};

enum class AreaObjectBoundsStatus : uint8_t {
    found,
    not_found,
    invalid_input,
};

struct AreaObjectBounds {
    nw::render::Bounds bounds{};
    uint32_t record_count = 0;
    AreaObjectBoundsStatus status = AreaObjectBoundsStatus::invalid_input;
};

// Click-driven batch selection against the indexed geometry owned by the
// current preview scene. Each query targets either live objects or tiles.
// Object queries include mesh-backed objects, triggers, and encounters. Tile
// hits carry kind/tile coordinates and an invalid object handle. Every hit
// carries its finite world-space intersection position. Trigger and encounter
// hits carry their live object handle and index the scene's cold debug-selection
// sidecar. Bounds reject non-candidates before geometry is read. Invalid rays
// produce invalid_input; stale records, invalid geometry, and rays that cross
// only a bound produce miss. The scene and record cache are borrowed for the
// call.
void select_area_objects(
    std::span<const AreaObjectRay> rays,
    const AreaRenderScene& records,
    const PreviewScene& scene,
    std::span<AreaObjectSelection> selections,
    AreaObjectSelectionOptions options = {});

// A pointer click is genuinely singular input. Keep it as a count-one wrapper
// over the batch transform so selection policy has one implementation.
[[nodiscard]] AreaObjectSelection select_area_object(
    const AreaObjectRay& ray,
    const AreaRenderScene& records,
    const PreviewScene& scene,
    AreaObjectSelectionOptions options = {});

// Batch reduction over current area-render record columns. Repeated records
// for one live object (body and attachments) produce one world-space bound.
// Disabled, mismatched, and non-finite records are dropped; malformed columns
// fail without scanning.
[[nodiscard]] AreaObjectBounds collect_area_object_bounds(
    nw::ObjectHandle object,
    std::span<const nw::render::Bounds> bounds,
    std::span<const uint8_t> flags,
    std::span<const AreaRenderRecordKind> kinds,
    std::span<const nw::ObjectHandle> objects) noexcept;

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
    uint32_t selectable_object_record_count = 0;
    uint32_t object_handle_bytes = 0;
    uint32_t opaque_cutout_record_count = 0;
    uint32_t water_record_count = 0;
    uint32_t transparent_record_count = 0;
    uint32_t shadow_caster_record_count = 0;
    uint32_t prepared_draw_count = 0;
    uint32_t surface_range_count = 0;
    uint32_t surface_triangle_count = 0;
    uint32_t surface_bytes = 0;
    uint32_t max_prepared_draws_per_record = 0;
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
    bool uses_cached_draw_lists = false;
};

struct AreaRenderCullContext {
    glm::mat4 view_projection{1.0f};
    std::span<const uint8_t> visible_chunk_mask;
    bool enabled = false;
    bool chunk_visibility_enabled = false;
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

class AreaRenderScene {
public:
    enum RecordFlag : uint8_t {
        render_enabled = 1u << 0u,
        static_candidate = 1u << 1u,
        shadow_caster = 1u << 2u,
    };

    void clear();
    void rebuild(const PreviewScene& scene);
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
    [[nodiscard]] uint32_t record_index_for_render_model(uint32_t model_index) const noexcept
    {
        return model_index < render_model_record_indices_.size()
            ? render_model_record_indices_[model_index]
            : kInvalidAreaRenderRecordIndex;
    }
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
    [[nodiscard]] std::span<const nw::ObjectHandle> object_handles() const noexcept { return object_handles_; }
    [[nodiscard]] std::span<const int16_t> tile_xs() const noexcept { return tile_xs_; }
    [[nodiscard]] std::span<const int16_t> tile_ys() const noexcept { return tile_ys_; }
    [[nodiscard]] std::span<const uint32_t> chunk_offsets() const noexcept { return chunk_offsets_; }
    [[nodiscard]] std::span<const uint32_t> chunk_record_indices() const noexcept { return chunk_record_indices_; }
    [[nodiscard]] std::span<const nw::render::Bounds> chunk_bounds() const noexcept { return chunk_bounds_; }
    [[nodiscard]] std::span<const uint8_t> chunk_has_bounds() const noexcept { return chunk_has_bounds_; }
    [[nodiscard]] std::span<const AreaSurfaceRange> surface_ranges() const noexcept { return surface_ranges_; }
    [[nodiscard]] std::span<const AreaSurfaceTriangle> surface_triangles() const noexcept { return surface_triangles_; }
    [[nodiscard]] AreaSurfaceHit trace_surface(const AreaObjectRay& ray) const noexcept
    {
        return trace_area_surface(ray, surface_ranges_, surface_triangles_);
    }
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
    [[nodiscard]] std::span<const uint32_t> light_indices() const noexcept { return light_indices_; }
    [[nodiscard]] std::span<const uint32_t> dynamic_light_indices() const noexcept { return dynamic_light_indices_; }
    [[nodiscard]] std::span<const uint32_t> light_indices_for_record(uint32_t record_index) const noexcept;
    [[nodiscard]] std::span<const uint32_t> chunk_light_indices() const noexcept { return chunk_light_indices_; }
    [[nodiscard]] std::span<const uint32_t> light_indices_for_chunk(uint32_t chunk_id) const noexcept;

private:
    std::vector<uint32_t> model_indices_;
    std::vector<uint32_t> render_model_record_indices_;
    std::vector<nw::render::ModelInstanceHandle> model_instance_handles_;
    std::vector<nw::render::Bounds> bounds_;
    std::vector<glm::mat4> root_transforms_;
    std::vector<uint8_t> pass_masks_;
    std::vector<uint8_t> flags_;
    std::vector<uint32_t> chunk_ids_;
    std::vector<AreaRenderRecordKind> kinds_;
    std::vector<nw::ObjectHandle> object_handles_;
    std::vector<int16_t> tile_xs_;
    std::vector<int16_t> tile_ys_;
    std::vector<nw::render::Bounds> chunk_bounds_;
    std::vector<uint8_t> chunk_has_bounds_;
    std::vector<uint32_t> chunk_offsets_;
    std::vector<uint32_t> chunk_record_indices_;
    std::vector<AreaSurfaceRange> surface_ranges_;
    std::vector<AreaSurfaceTriangle> surface_triangles_;
    nw::render::PreparedModelDrawList prepared_model_draws_;
    nw::render::PreparedModelDrawRangeList prepared_model_draw_ranges_;
    nw::render::PreparedModelSurfaceDrawList prepared_model_surface_draws_;
    std::vector<uint32_t> prepared_surface_offsets_;
    std::vector<uint32_t> prepared_surface_indices_;
    std::array<uint32_t, 5> prepared_surface_pass_offsets_{};
    std::vector<uint32_t> light_index_offsets_;
    std::vector<uint32_t> light_indices_;
    std::vector<uint32_t> dynamic_light_indices_;
    std::vector<uint32_t> chunk_light_index_offsets_;
    std::vector<uint32_t> chunk_light_indices_;
    nw::render::Bounds scene_bounds_{};
    AreaRenderSceneStats stats_{};
    bool has_scene_bounds_ = false;
};

// The active viewport selection is a true singleton. Tile hits retain their
// exact triangle intersection, while this derives a stable, shallow 10x10 m
// box rooted at the selected tile record's authored elevation. Stale,
// mismatched, disabled, or non-finite records have no outline.
[[nodiscard]] std::optional<nw::render::Bounds> area_tile_selection_bounds(
    const AreaObjectSelection& selection,
    const AreaRenderScene& records) noexcept;

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
    [[nodiscard]] bool uses_cached_draw_lists() const noexcept { return cached_draw_scene_ != nullptr; }
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
    std::vector<uint32_t> visible_light_indices_;
    std::vector<uint32_t> visible_prepared_surface_indices_;
    std::array<uint32_t, 5> visible_prepared_surface_pass_offsets_{};
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
};

[[nodiscard]] std::string_view area_render_record_kind_label(AreaRenderRecordKind kind) noexcept;
[[nodiscard]] bool should_use_sorted_area_static_surface_lists(
    uint32_t visible_prepared_surface_count,
    uint32_t total_prepared_surface_count) noexcept;
size_t rebuild_area_visibility_mask(
    const AreaRenderScene& scene,
    const AreaVisibilityMaskOptions& options,
    std::vector<uint8_t>& mask);
void prepare_area_frame(
    const AreaRenderScene& scene,
    AreaRenderFrame& frame,
    const AreaRenderCullContext& cull = {});

} // namespace nw::render::viewer
