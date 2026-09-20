#pragma once

#include "area_render_scene.hpp"
#include "preview_load_report.hpp"
#include "preview_nwn_creature.hpp"
#include "preview_render_resources.hpp"

#include <nw/model/mdl_particle_import.hpp>
#include <nw/render/gltf/import_gltf.hpp>
#include <nw/render/model_instance_attachment.hpp>
#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_def.hpp>
#include <nw/render/particle_system.hpp>

#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

namespace nw::render::viewer {

using nw::render::Bounds;
using nw::render::gltf::ImportGltfDesc;

enum class PreferredModelAnimationContext {
    sequence_effect,
    particle_preview,
    hold,
};

struct PreviewSceneLoadOptions {
    nw::ObjectVisualRenderMode visual_render_mode = nw::ObjectVisualRenderMode::toolset;
    bool area_object_editing = false;
};

struct PreviewRuntimeModelReport {
    std::string owner;
    uint32_t model_index = std::numeric_limits<uint32_t>::max();
    bool instance_handle_valid = false;
    bool instance_present = false;
    bool visible = false;
    size_t primitive_count = 0;
    size_t skinned_primitive_count = 0;
    size_t skin_count = 0;
    size_t skeleton_count = 0;
    size_t animation_count = 0;
    size_t particle_system_count = 0;
    uint32_t selected_clip_index = std::numeric_limits<uint32_t>::max();
    std::string selected_clip_name;
    float clip_time = 0.0f;
    bool animation_enabled = false;
    bool animation_backend_ready = false;
    bool animation_looping = false;
    size_t skin_matrix_table_count = 0;
    size_t skin_matrix_row_count = 0;
    size_t attachment_node_count = 0;
    size_t valid_attachment_node_count = 0;
    size_t scene_particle_system_count = 0;
    size_t scene_particle_emitter_count = 0;
    size_t scene_particle_active_emitter_count = 0;
    uint32_t scene_particle_max_particles_total = 0;
    size_t scene_particle_event_count = 0;
    size_t scene_particle_live_particle_count = 0;
};

struct PreviewRuntimeModelReports {
    // Batch transform contract:
    // input layout: scene-owned RenderModel assets, common ModelInstance
    // handles, live SceneParticleSystem rows, and optional frame-owned prepared
    // skin table stats. output layout: caller-owned flat rows keyed by
    // model_index. owner/lifetime: strings and counts are copied into the
    // returned report; no pointers into the scene escape. valid ranges:
    // model_index is the static_models index, or uint32 max when unavailable;
    // selected_clip_index is uint32 max when no selected clip can be named.
    // Missing/stale instances emit a row with instance_present=false and zero
    // runtime-only counts.
    std::vector<PreviewRuntimeModelReport> models;
    size_t render_model_count = 0;
    size_t skinned_model_count = 0;
    size_t animated_model_count = 0;
    size_t animation_enabled_model_count = 0;
    size_t animation_backend_ready_model_count = 0;
    size_t skin_matrix_row_count = 0;
    size_t scene_particle_system_count = 0;
    size_t scene_particle_emitter_count = 0;
    size_t scene_particle_live_particle_count = 0;
    bool prepared_skin_table_available = false;
    nw::render::PreparedRenderModelSkinTableStats prepared_skin_table_stats{};
};

struct SceneParticleSystem {
    SceneParticleSystem() = default;
    SceneParticleSystem(SceneParticleSystem&& other) noexcept;
    SceneParticleSystem& operator=(SceneParticleSystem&& other) noexcept;
    SceneParticleSystem(const SceneParticleSystem&) = delete;
    SceneParticleSystem& operator=(const SceneParticleSystem&) = delete;

    // Common handle/index supplies owner visibility, root transform, and dense
    // attachment-point rows. The owner is always a RenderModel scene row.
    uint32_t owner_model_index = std::numeric_limits<uint32_t>::max();
    nw::render::ModelInstanceHandle owner_instance_handle;
    nw::model::ParticleImportResult import;
    nw::render::ParticleCompileResult compiled;
    // Invariant: a non-null system.effect points at this row's compiled.effect.
    // The move operations repair that self-reference after vector relocation.
    nw::render::ParticleSystemInstance system;
    float animation_time = 0.0f;
    float particle_animation_length = 0.0f;
    bool animation_time_initialized = false;
    bool owner_visible_last = true;
};

inline constexpr uint32_t kInvalidSceneModelAttachmentBindingIndex = std::numeric_limits<uint32_t>::max();

struct PreviewSceneRuntimeSyncStats {
    uint32_t render_model_count = 0;
    uint32_t render_model_attachment_binding_count = 0;
    uint32_t render_model_attachment_root_resolved_count = 0;
    uint32_t render_model_attachment_root_failed_count = 0;
};

struct RenderModelAttachmentSetup {
    // Setup-only row. Socket names and this row are borrowed for the duration
    // of attach_render_models; the scene retains only resolved indices, handles,
    // transforms, and policies.
    uint32_t child_model_index = nw::render::kInvalidModelInstanceIndex;
    uint32_t owner_model_index = nw::render::kInvalidModelInstanceIndex;
    std::string_view owner_socket;
    std::string_view child_source_socket;
    glm::mat4 child_local_transform{1.0f};
    float child_local_scale = 1.0f;
    nw::render::ModelAttachmentOrientationPolicy orientation = nw::render::ModelAttachmentOrientationPolicy::socket;
    nw::render::ModelAttachmentSourceOffsetPolicy source_offset = nw::render::ModelAttachmentSourceOffsetPolicy::socket_bind;
};

struct RenderModelAttachmentSetupStats {
    uint32_t input_count = 0;
    uint32_t attached_count = 0;
    uint32_t invalid_model_count = 0;
    uint32_t missing_owner_socket_count = 0;
    uint32_t invalid_transform_count = 0;
    uint32_t invalid_scale_count = 0;
    uint32_t binding_limit_count = 0;
};

struct AreaObjectSpatialUpdateStats {
    uint32_t input_count = 0;
    uint32_t rejected_input_count = 0;
    uint32_t render_model_root_count = 0;
    uint32_t debug_shape_vertex_count = 0;
};

struct AreaObjectModelIndexEntry {
    nw::ObjectHandle object;
    uint32_t model_index = nw::render::kInvalidModelInstanceIndex;
};

enum class AreaObjectPreviewAppendStatus : uint8_t {
    success,
    empty,
    invalid_input,
    failed,
};

struct AreaObjectPreviewAppendResult {
    AreaObjectPreviewAppendStatus status = AreaObjectPreviewAppendStatus::empty;
    uint32_t object_count = 0;
    uint32_t model_count = 0;
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == AreaObjectPreviewAppendStatus::success;
    }
};

enum class AreaTransientVisualStatus : uint8_t {
    success,
    empty,
    invalid_input,
    failed,
};

struct AreaTransientVisualResult {
    AreaTransientVisualStatus status = AreaTransientVisualStatus::empty;
    uint32_t object_count = 0;
    uint32_t model_count = 0;
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == AreaTransientVisualStatus::success;
    }
};

struct AreaTilePreviewRow {
    uint32_t tile_index = 0;
    int32_t tile_id = -1;
    int32_t height = 0;
    int32_t orientation = 0;
};

struct AreaTilePreviewLease {
    // Authored tile rows covered by the transient preview. Their common model
    // instances remain unchanged; the area render frame suppresses these
    // stable rows while drawing the preview batch.
    std::vector<uint32_t> suppressed_tile_model_indices;
    // Object handles identify replacement-model rows for the existing batch
    // removal transform; a suppression-only void preview leaves this empty.
    // These handles do not refer to live kernel objects.
    std::vector<nw::ObjectHandle> preview_objects;
    // Dense scene rows for the retained preview instances. When the next
    // batch uses the same models in the same order, only their transforms and
    // the suppressed authored-row batch change.
    std::vector<uint32_t> preview_model_indices;
    // Keeps the last preview batch's immutable assets alive after its scene
    // rows are removed, through the immediately following commit/undo refresh.
    // The next preview batch replaces these references.
    std::vector<std::shared_ptr<nw::render::RenderModel>> retained_models;
    bool active = false;
};

enum class ObjectVisualRefreshStatus : uint8_t {
    success,
    empty,
    invalid_input,
    failed,
};

struct ObjectVisualRefreshResult {
    ObjectVisualRefreshStatus status = ObjectVisualRefreshStatus::empty;
    uint32_t object_count = 0;
    uint32_t removed_model_count = 0;
    uint32_t added_model_count = 0;
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == ObjectVisualRefreshStatus::success;
    }
};

struct DebugShapeVertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{0.0f};
};

struct AreaTileGridVertex {
    glm::vec3 position{0.0f};
    glm::vec3 opposite{0.0f};
    glm::vec2 extrusion{0.0f};
    glm::vec4 color{0.0f};
};

struct AreaTileGridDebugGeometry {
    std::vector<AreaTileGridVertex> vertices;
    std::vector<uint32_t> indices;
};

// Batch transform contract: input is one dense width*height Area tile array
// with a finite positive tileset height. Output is one caller-owned screen-line
// triangle batch. Contiguous equal-height edges are merged; height breaks occur
// at both elevations. Invalid input or allocation failure clears output.
[[nodiscard]] bool build_area_tile_grid_debug_geometry(
    const nw::Area& area,
    AreaTileGridDebugGeometry& output) noexcept;

enum class DebugShapeCategory : uint8_t {
    general,
    trigger,
    encounter,
    sound,
    store,
    waypoint,
};

struct DebugShapeRange {
    DebugShapeCategory category = DebugShapeCategory::general;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
};

struct DebugShapeSelectionRange {
    Bounds bounds{};
    nw::ObjectHandle object{};
    uint32_t debug_shape_range_index = kInvalidAreaRenderRecordIndex;
    uint32_t first_point = 0;
    uint32_t point_count = 0;
    uint32_t subindex = UINT32_MAX;
    float plane_z = 0.0f;
    DebugShapeCategory category = DebugShapeCategory::general;
};

// CPU/GPU protocol for the sound-range dot draw. Rows are contiguous and
// consumed by one instanced draw; center_radius.w is strictly positive.
struct SoundDebugDotInstance {
    glm::vec4 center_radius{0.0f};
    glm::vec4 normal{0.0f};
    glm::vec4 color{0.0f};
};

static_assert(std::is_standard_layout_v<SoundDebugDotInstance>);
static_assert(sizeof(SoundDebugDotInstance) == 48);

struct DebugShapeObjectRange {
    nw::ObjectHandle object{};
    glm::vec3 root_position{0.0f};
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    uint32_t first_sound_dot = 0;
    uint32_t sound_dot_count = 0;
};

struct SceneTileLightSlots {
    uint8_t main1 = 0;
    uint8_t main2 = 0;
    uint8_t source1 = 0;
    uint8_t source2 = 0;
};

enum class SceneLocalLightSource : uint8_t {
    authored_model,
    tile_model,
    placeable_table,
};

inline constexpr uint32_t kInvalidSceneLocalLightModelIndex = std::numeric_limits<uint32_t>::max();

struct SceneLocalLight {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float base_radius = 0.0f;
    float base_intensity = 1.0f;
    SceneLocalLightSource source = SceneLocalLightSource::authored_model;
    uint32_t model_index = kInvalidSceneLocalLightModelIndex;
    int32_t model_source_node_index = -1;
    SceneTileLightSlots tile_light_slots{};
    uint16_t tile_x = 0;
    uint16_t tile_y = 0;
    uint8_t tile_orientation = 0;
    uint8_t dynamic = 0;
    uint8_t affect_dynamic = 0;
    uint8_t ambient_contribution = 0;
    uint8_t casts_shadow = 0;
    uint8_t fading = 0;
};

// Timings for the last live AreaTile batch refresh. Inputs are sorted unique
// tile indices; outputs are the replaced scene rows and their derived caches.
// Durations cover CPU work only and remain zero when validation rejects input.
struct AreaTileRefreshStats {
    uint32_t changed_tile_count = 0;
    uint32_t retained_model_count = 0;
    uint32_t replaced_model_count = 0;
    float model_prepare_seconds = 0.0f;
    float instance_refresh_seconds = 0.0f;
    float light_refresh_seconds = 0.0f;
    float scene_summary_seconds = 0.0f;
    float record_refresh_seconds = 0.0f;
};

struct PreviewScene {
    PreviewScene() = default;
    ~PreviewScene();

    PreviewScene(const PreviewScene&) = delete;
    PreviewScene& operator=(const PreviewScene&) = delete;

    // Immutable-by-convention RenderModel assets referenced by indexed common
    // ModelInstance rows. Repeated area tile placements share one uploaded
    // asset; refcounts change only while scenes are built or destroyed, never
    // in the frame traversal.
    std::vector<std::shared_ptr<nw::render::RenderModel>> static_models;
    std::vector<nw::render::ModelInstanceHandle> static_model_instance_handles;
    std::vector<uint32_t> static_model_attachment_binding_indices;
    std::vector<AreaRenderSourceInfo> static_area_model_info;
    // Loaded once with the area. Row-major area tile index -> stable original
    // model row; missing tile models leave an invalid index.
    std::vector<uint32_t> area_tile_model_indices;
    AreaTileRefreshStats last_area_tile_refresh_stats;
    nw::render::ModelInstanceStore model_instances;
    nw::render::ModelMaterialOverrideStore material_overrides;
    std::unique_ptr<AreaRenderScene> area_render_scene;
    std::vector<nw::render::ModelInstanceAttachmentBinding> model_attachments;
    // Main-thread frame scratch retained across updates. Rows correspond 1:1
    // with model_attachments.
    std::vector<nw::render::ModelAttachmentRootTransformInput> attachment_transform_inputs;
    std::vector<nw::render::ModelAttachmentRootTransformOutput> attachment_transform_outputs;
    // Runtime-update index and retained scratch. The index is rebuilt once
    // after scene topology changes, then spatial updates touch only models
    // owned by the changed object rows and their attachment descendants.
    std::vector<AreaObjectModelIndexEntry> area_object_model_indices;
    std::vector<uint32_t> attachment_owner_offsets;
    std::vector<uint32_t> attachment_owner_binding_indices;
    std::vector<uint32_t> local_light_owner_offsets;
    std::vector<uint32_t> local_light_owner_indices;
    std::vector<uint32_t> spatial_update_model_scratch;
    std::vector<uint32_t> runtime_sync_model_scratch;
    std::vector<uint32_t> runtime_sync_binding_scratch;
    std::vector<uint32_t> particle_rebuild_model_scratch;
    std::vector<nw::ObjectHandle> spatial_update_owner_scratch;
    bool runtime_update_indices_dirty = true;
    std::vector<SceneParticleSystem> particles;
    std::vector<DebugShapeVertex> debug_shape_vertices;
    std::vector<uint32_t> debug_shape_indices;
    std::vector<DebugShapeRange> debug_shape_ranges;
    std::vector<SoundDebugDotInstance> sound_debug_dot_instances;
    std::vector<glm::vec3> debug_shape_selection_points;
    std::vector<DebugShapeSelectionRange> debug_shape_selection_ranges;
    std::vector<DebugShapeObjectRange> debug_shape_object_ranges;
    std::vector<SceneLocalLight> local_lights;
    std::vector<nw::render::LocalLight> render_local_lights;
    std::string hold_animation;
    size_t vertex_count = 0;
    size_t index_count = 0;
    Bounds bounds{};
    bool is_area = false;
    int area_width = 0;
    int area_height = 0;
    float area_overlay_z = 0.0f;
    nw::AreaFlags area_flags = nw::AreaFlags::none;
    nw::AreaWeather area_weather{};
    bool has_water = false;
    // Scene-level model provenance. glTF previews are authored with their own
    // lighting baked into the asset and render unlit; NWN and native content
    // needs the viewer to supply lighting. Maintained alongside has_water so no
    // consumer has to re-derive the policy by scanning static_models per frame.
    bool has_gltf_models = false;
    std::vector<PreviewLoadEvent> source_load_events;
    PreviewLoadReport load_report;
    // Loaded scenes own the root used to derive render rows. A replacement area
    // scene borrows that root until ViewerSession completes the ownership transfer.
    // The selected contained object is always non-owning.
    nw::ObjectHandle root_object;
    nw::ObjectHandle active_object;
    bool owns_root_object = true;

    void rebuild_load_report(std::string_view source, std::string_view kind);
    void rebuild_particles(std::string_view animation_name = {});
    // Replaces particle rows only for the borrowed dense model-index batch.
    // Duplicate and out-of-range indices are ignored. Unselected model rows
    // and standalone particle effects retain their live state.
    void rebuild_model_particles(std::span<const uint32_t> model_indices);
    void update(int32_t dt_ms);
    void set_particle_target_point(
        nw::render::ModelInstanceHandle owner_handle, uint32_t owner_model_index, const glm::vec3& target_point);
    Bounds current_bounds() const;
    bool contains_water() const noexcept;
    nw::render::ModelInstance* static_model_instance(size_t model_index) noexcept;
    const nw::render::ModelInstance* static_model_instance(size_t model_index) const noexcept;
    void add(std::unique_ptr<nw::render::RenderModel> model);
    void add(std::shared_ptr<nw::render::RenderModel> model);
    void add_attached(std::unique_ptr<nw::render::RenderModel> model, uint32_t owner_model_index,
        std::string_view owner_socket, std::string_view child_source_socket = {}, float child_local_scale = 1.0f);
    // Resolves setup names for a batch of already-added RenderModel rows and
    // stores only the common indexed attachment protocol. Invalid rows are
    // counted and left unattached; valid rows in the same batch still commit.
    RenderModelAttachmentSetupStats attach_render_models(
        std::span<const RenderModelAttachmentSetup> attachments);
    void add_particle_effect(nw::render::ParticleEffectDef effect);
    void invalidate_runtime_update_indices() noexcept;
    void rebuild_runtime_update_indices();
};

// Batch transform from scene-owned assets into common instance runtime state.
// Inputs are PreviewScene RenderModel rows plus stable instance handles.
// Outputs are common ModelInstance visibility, root transform, world-space
// current bounds, and world-space shadow summary. Stale handles, mismatched
// indices, and missing assets are skipped.
PreviewSceneRuntimeSyncStats sync_model_instance_runtime_state(PreviewScene& scene);
PreviewSceneRuntimeSyncStats sync_model_instance_runtime_state(
    PreviewScene& scene, std::span<const uint32_t> root_model_indices);

// Batch transform from live or preview spatial rows into scene-owned model
// roots. Inputs are borrowed for the call; the scene retains only the resulting
// matrices. Invalid owners, non-finite values, non-positive scales, missing
// objects, and attachment children are skipped. The first valid row wins when
// an owner is repeated.
AreaObjectSpatialUpdateStats update_area_object_spatial_states(
    PreviewScene& scene, std::span<const nw::ObjectSpatialState> spatial_states);

PreviewSceneLoadOptions default_preview_scene_load_options();
std::unique_ptr<PreviewScene> load_preview_scene(PreviewRenderResources& resources, std::string_view source);
std::unique_ptr<PreviewScene> load_preview_scene(
    PreviewRenderResources& resources, std::string_view source, PreviewSceneLoadOptions options);
std::unique_ptr<PreviewScene> load_preview_scene(PreviewRenderResources& resources, std::span<const std::string> sources);
std::unique_ptr<PreviewScene> load_area_scene(PreviewRenderResources& resources, std::string_view area_resref);
std::unique_ptr<PreviewScene> load_area_scene(
    PreviewRenderResources& resources, std::string_view area_resref, PreviewSceneLoadOptions options);
// Builds render rows from an already-instantiated Area without taking ownership.
// ViewerSession transfers ownership only after the replacement scene succeeds.
std::unique_ptr<PreviewScene> build_live_area_scene(
    PreviewRenderResources& resources,
    nw::Area& area,
    std::string_view source,
    PreviewSceneLoadOptions options);
// Replaces a sorted batch of already-committed live area tile rows in place.
// Stable scene model indices and instance handles are retained. Invalid input,
// missing models, or debug-light geometry reject before scene mutation so the
// caller can rebuild the complete live area.
[[nodiscard]] AreaTransientVisualResult refresh_live_area_tiles(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const uint32_t> tile_indices);
// Builds render rows from one already-instantiated live object without
// taking ownership. Invalid handles and unsupported object types fail.
std::unique_ptr<PreviewScene> build_live_object_scene(
    PreviewRenderResources& resources,
    nw::ObjectHandle object,
    std::string_view source,
    PreviewSceneLoadOptions options);
// Appends detached live Creature/Placeable/Item visuals as one translucent preview
// batch. Inputs are validated and built before the destination scene changes.
[[nodiscard]] AreaObjectPreviewAppendResult append_area_object_previews(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const nw::ObjectHandle> objects,
    float opacity,
    PreviewSceneLoadOptions options);
// Replaces the current tile preview with one exact replacement per row and
// suppresses the corresponding authored rows at area-frame submission. Rows
// are a borrowed, strictly increasing row-major tile-index batch; the scene
// owns appended rows until the lease is restored. An active lease with the
// same ordered model layout is repositioned without rebuilding the area
// render cache or changing authored ModelInstance state.
[[nodiscard]] AreaTransientVisualResult update_area_tile_previews(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const AreaTilePreviewRow> rows,
    AreaTilePreviewLease& lease);
[[nodiscard]] AreaTransientVisualResult restore_area_tile_previews(
    PreviewScene& scene, AreaTilePreviewLease& lease);
// Adds opaque, shadow-casting visuals for detached runtime objects without
// changing editor selection. The scene owns the appended render rows until the
// same object-handle batch is removed.
[[nodiscard]] AreaTransientVisualResult append_area_transient_visuals(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const nw::ObjectHandle> objects,
    PreviewSceneLoadOptions options);
// Removes every render row owned by the input handles. Object validity is not
// required, so presentation teardown may follow object-lifetime teardown.
[[nodiscard]] AreaTransientVisualResult remove_area_transient_visuals(
    PreviewScene& scene,
    std::span<const nw::ObjectHandle> objects);
// Batch replacement of live Creature or Item visual rows. The input is a
// borrowed flat span of unique live handles already represented by the scene;
// unsupported, duplicate, missing, or unrepresented rows reject the complete
// batch. All replacement assets are built before scene mutation, and the scene
// retains the appended render rows for its lifetime.
[[nodiscard]] ObjectVisualRefreshResult refresh_object_visuals(
    PreviewScene& scene,
    PreviewRenderResources& resources,
    std::span<const nw::ObjectHandle> objects,
    PreviewSceneLoadOptions options);
PreviewRuntimeModelReports build_preview_runtime_model_reports(
    const PreviewScene& scene,
    const nw::render::PreparedModelSurfaceDrawList* prepared_surfaces = nullptr);
Lighting studio_preview_lighting();
LightingSpace studio_preview_lighting_space() noexcept;
int32_t compute_particle_prime_ms(const PreviewScene& scene, bool explicit_animation);
std::string_view preferred_model_animation_name(
    const nw::model::Mdl& mdl, PreferredModelAnimationContext context);
} // namespace nw::render::viewer
