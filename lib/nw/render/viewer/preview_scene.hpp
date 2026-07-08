#pragma once

#include "area_render_scene.hpp"
#include "preview_render_resources.hpp"

#include <nw/model/mdl_particle_import.hpp>
#include <nw/render/gltf/import_gltf.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_def.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/resources/assets.hpp>

#include <nw/objects/Area.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace nw {
struct Appearance;
struct StaticTwoDA;
}

namespace nw::render::viewer {

using nw::render::gltf::ImportGltfDesc;
using nw::render::nwn::Bounds;
using nw::render::nwn::ModelInstance;

enum class PreferredModelAnimationContext {
    sequence_effect,
    particle_preview,
    hold,
};

enum class NwnModelPreviewPath : uint8_t {
    legacy,
    render_model,
};

struct PreviewSceneLoadOptions {
    // Bridge toggle for NWN model previews. The render_model path is the
    // default: it imports static MDL previews and single-model creature
    // appearances through ModelAsset, then uploads them as source-agnostic
    // RenderModel data. The legacy path keeps nwn::ModelInstance as the sidecar
    // owner for PLT, emitter, attachment, and animation quirks and remains
    // available as the compatibility fallback. Humanoid body-part/equipment
    // assembly stays legacy until PLT/socket/cross-skeleton policy is lowered
    // into explicit common data.
    NwnModelPreviewPath nwn_model_path = NwnModelPreviewPath::render_model;
};

enum class NwnAppearanceHandItemVisualPolicyReason : uint8_t {
    visible,
    hidden_no_arms,
    hidden_null_weapon_scale,
    hidden_invalid_weapon_scale,
};

struct NwnAppearanceHandItemVisualPolicy {
    bool visible = true;
    float scale = 1.0f;
    NwnAppearanceHandItemVisualPolicyReason reason = NwnAppearanceHandItemVisualPolicyReason::visible;
};

// NWN appearance rows use WEAPONSCALE/HASARMS as held-item visual policy. This
// applies to both hand slots: right-hand weapons and left-hand shields/offhand
// items are either both hidden or both scaled by the appearance value.
NwnAppearanceHandItemVisualPolicy resolve_nwn_appearance_hand_item_visual_policy(
    const nw::StaticTwoDA* appearance_tda,
    nw::Appearance appearance_id);

enum class PreviewLoadResourceStatus {
    found,
    missing,
};

enum class PreviewLoadEventSeverity {
    info,
    warning,
    error,
};

struct PreviewLoadResource {
    nw::Resource resource;
    PreviewLoadResourceStatus status = PreviewLoadResourceStatus::found;
    std::vector<std::string> origins;
};

struct PreviewLoadEvent {
    PreviewLoadEventSeverity severity = PreviewLoadEventSeverity::info;
    std::string category;
    std::string message;
};

struct PreviewLoadParticleSystemReport {
    std::string owner;
    size_t emitter_count = 0;
    size_t material_count = 0;
    size_t alpha_material_count = 0;
    size_t cutout_material_count = 0;
    size_t additive_material_count = 0;
    size_t named_texture_count = 0;
    size_t missing_texture_count = 0;
    size_t alpha_texture_count = 0;
    size_t opaque_texture_count = 0;
    uint32_t max_particles_total = 0;
    size_t import_warning_count = 0;
    size_t compile_warning_count = 0;
    size_t effect_event_count = 0;
};

struct PreviewLoadMaterialReport {
    std::string owner;
    size_t material_count = 0;
    size_t fallback_material_count = 0;
    size_t plt_albedo_count = 0;
    size_t plt_enabled_count = 0;
    size_t emissive_material_count = 0;
    size_t double_sided_count = 0;
    size_t opaque_count = 0;
    size_t cutout_count = 0;
    size_t transparent_count = 0;
    size_t water_count = 0;
    size_t unknown_alpha_mode_count = 0;
    size_t color_key_count = 0;
    size_t albedo_source_count = 0;
    size_t normal_source_count = 0;
    size_t surface_source_count = 0;
    size_t emissive_source_count = 0;
    size_t albedo_bound_count = 0;
    size_t normal_bound_count = 0;
    size_t surface_bound_count = 0;
    size_t emissive_bound_count = 0;
    size_t invalid_scalar_count = 0;
    float roughness_min = 0.0f;
    float roughness_max = 0.0f;
    float specular_strength_min = 0.0f;
    float specular_strength_max = 0.0f;
    float normal_scale_min = 0.0f;
    float normal_scale_max = 0.0f;
};

struct PreviewLoadGeometryReport {
    std::string owner;
    size_t primitive_count = 0;
    size_t static_primitive_count = 0;
    size_t skinned_primitive_count = 0;
    size_t deformed_primitive_count = 0;
    size_t shadow_caster_count = 0;
    size_t vertex_count = 0;
    size_t index_count = 0;
    size_t node_count = 0;
    size_t socket_count = 0;
    size_t skin_count = 0;
    size_t skeleton_count = 0;
    size_t animation_count = 0;
    size_t deformer_count = 0;
    size_t particle_system_count = 0;
    size_t normal_repair_count = 0;
    size_t tangent_repair_count = 0;
    size_t skipped_empty_mesh_count = 0;
    size_t skipped_skin_mesh_count = 0;
    size_t primitive_overflow_count = 0;
};

struct PreviewLoadReport {
    std::string source;
    std::string kind;
    std::vector<std::string> model_names;
    std::vector<PreviewLoadResource> resources;
    std::vector<PreviewLoadMaterialReport> materials;
    std::vector<PreviewLoadGeometryReport> geometries;
    std::vector<PreviewLoadParticleSystemReport> particles;
    std::vector<PreviewLoadEvent> events;

    size_t missing_resource_count() const;
    size_t warning_count() const;
    size_t error_count() const;
};

struct PreviewRuntimeModelReport {
    std::string owner;
    nw::render::ModelInstanceKind kind = nw::render::ModelInstanceKind::render_model;
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

struct MeshTriangle {
    glm::vec3 v0, v1, v2;
};

struct ModelMeshCollisionProvider : public nw::render::ParticleCollisionProvider {
    std::vector<MeshTriangle> triangles;
    nw::render::ParticleCollisionQuery trace_particle(glm::vec3 from, glm::vec3 to, float radius) const override;
};

struct SceneParticleSystem {
    // Legacy sidecar supplies source-node fallback lookup during the bridge.
    // Common handle/kind/index supplies owner visibility/root transform for
    // both legacy sidecars and source-agnostic RenderModel owners.
    const ModelInstance* owner = nullptr;
    nw::render::ModelInstanceKind owner_kind = nw::render::ModelInstanceKind::nwn_legacy;
    uint32_t owner_model_index = std::numeric_limits<uint32_t>::max();
    nw::render::ModelInstanceHandle owner_instance_handle;
    nw::model::ParticleImportResult import;
    nw::render::ParticleCompileResult compiled;
    nw::render::ParticleSystemInstance system;
    std::unique_ptr<ModelMeshCollisionProvider> collision;
    // World-space mesh AABB used to kill particles that escape the model geometry.
    // Valid when collision is non-null.
    glm::vec3 mesh_aabb_min{0.0f};
    glm::vec3 mesh_aabb_max{0.0f};
    float animation_time = 0.0f;
    float particle_animation_length = 0.0f;
    bool animation_time_initialized = false;
    bool owner_visible_last = true;
};

struct SceneModelAttachmentBinding {
    // Bridge record for sidecar transform anchors. The common owner handle and
    // socket indices are scene-owned runtime data. `*_model_index` addresses
    // `models` when the matching kind is nwn_legacy and `static_models` when
    // the matching kind is render_model. nwn::ModelInstance keeps the legacy
    // pointer/string anchor as fallback until transform evaluation moves fully
    // into the common instance path.
    nw::render::ModelInstanceKind child_kind = nw::render::ModelInstanceKind::nwn_legacy;
    uint32_t child_model_index = std::numeric_limits<uint32_t>::max();
    nw::render::ModelInstanceHandle child_instance_handle;
    nw::render::ModelInstanceKind owner_kind = nw::render::ModelInstanceKind::nwn_legacy;
    uint32_t owner_model_index = std::numeric_limits<uint32_t>::max();
    nw::render::ModelInstanceHandle owner_instance_handle;
    uint32_t owner_socket_index = nw::render::kInvalidModelNodeIndex;
    uint32_t child_source_socket_index = nw::render::kInvalidModelNodeIndex;
    float child_local_scale = 1.0f;
};

inline constexpr uint32_t kInvalidSceneModelAttachmentBindingIndex = std::numeric_limits<uint32_t>::max();

struct PreviewSceneRuntimeSyncStats {
    uint32_t nwn_model_count = 0;
    uint32_t render_model_count = 0;
    uint32_t nwn_attachment_binding_count = 0;
    uint32_t nwn_attachment_root_resolved_count = 0;
    uint32_t nwn_attachment_root_failed_count = 0;
    uint32_t render_model_attachment_binding_count = 0;
    uint32_t render_model_attachment_root_resolved_count = 0;
    uint32_t render_model_attachment_root_failed_count = 0;
};

struct DebugShapeVertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{0.0f};
};

enum class DebugShapeCategory : uint8_t {
    general,
    trigger,
    encounter,
};

struct DebugShapeRange {
    DebugShapeCategory category = DebugShapeCategory::general;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
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

struct PreviewScene {
    std::vector<std::unique_ptr<ModelInstance>> models;
    std::vector<nw::render::ModelInstanceHandle> model_instance_handles;
    std::vector<uint32_t> model_attachment_binding_indices;
    std::vector<AreaRenderSourceInfo> area_model_info;
    std::vector<std::unique_ptr<nw::render::RenderModel>> static_models;
    std::vector<nw::render::ModelInstanceHandle> static_model_instance_handles;
    std::vector<uint32_t> static_model_attachment_binding_indices;
    std::vector<AreaRenderSourceInfo> static_area_model_info;
    nw::render::ModelInstanceStore model_instances;
    nw::render::ModelMaterialOverrideStore material_overrides;
    std::unique_ptr<AreaRenderScene> area_render_scene;
    std::vector<SceneModelAttachmentBinding> model_attachments;
    std::vector<SceneParticleSystem> particles;
    std::vector<DebugShapeVertex> debug_shape_vertices;
    std::vector<uint32_t> debug_shape_indices;
    std::vector<DebugShapeRange> debug_shape_ranges;
    std::vector<SceneLocalLight> local_lights;
    std::vector<nw::render::LocalLight> render_local_lights;
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
    std::vector<PreviewLoadEvent> source_load_events;
    PreviewLoadReport load_report;

    bool load_animation(std::string_view name);
    bool load_default_animations(PreferredModelAnimationContext context = PreferredModelAnimationContext::hold);
    void rebuild_load_report(std::string_view source, std::string_view kind);
    void rebuild_particles(std::string_view animation_name = {});
    void update(int32_t dt_ms);
    void set_particle_target_point(
        nw::render::ModelInstanceHandle owner_handle, uint32_t owner_model_index, const glm::vec3& target_point);
    void set_particle_target_point(const ModelInstance* owner, const glm::vec3& target_point);
    Bounds current_bounds() const;
    bool contains_water() const noexcept;
    nw::render::ModelInstance* nwn_model_instance(size_t model_index) noexcept;
    const nw::render::ModelInstance* nwn_model_instance(size_t model_index) const noexcept;
    nw::render::ModelInstance* static_model_instance(size_t model_index) noexcept;
    const nw::render::ModelInstance* static_model_instance(size_t model_index) const noexcept;
    void add(std::unique_ptr<ModelInstance> model);
    void add_attached(std::unique_ptr<ModelInstance> model, uint32_t owner_model_index,
        std::string_view owner_socket, std::string_view child_source_socket = {});
    void add(std::unique_ptr<nw::render::RenderModel> model);
    void add_attached(std::unique_ptr<nw::render::RenderModel> model, uint32_t owner_model_index,
        std::string_view owner_socket, std::string_view child_source_socket = {}, float child_local_scale = 1.0f);
    void add_particle_effect(nw::render::ParticleEffectDef effect);
};

// Batch transform from scene-owned sidecar/assets into common instance runtime
// state. Inputs are PreviewScene model arrays plus stable instance handles.
// Outputs are common ModelInstance visibility, root transform, world-space
// current bounds, and world-space shadow summary. Stale handles, mismatched
// indices, and missing assets are skipped.
PreviewSceneRuntimeSyncStats sync_model_instance_runtime_state(PreviewScene& scene);

PreviewSceneLoadOptions default_preview_scene_load_options();
std::unique_ptr<PreviewScene> load_preview_scene(PreviewRenderResources& resources, std::string_view source);
std::unique_ptr<PreviewScene> load_preview_scene(
    PreviewRenderResources& resources, std::string_view source, PreviewSceneLoadOptions options);
std::unique_ptr<PreviewScene> load_preview_scene(PreviewRenderResources& resources, std::span<const std::string> sources);
std::unique_ptr<PreviewScene> load_preview_scene(
    PreviewRenderResources& resources, std::span<const std::string> sources, PreviewSceneLoadOptions options);
std::unique_ptr<PreviewScene> load_area_scene(PreviewRenderResources& resources, std::string_view area_resref);
std::unique_ptr<PreviewScene> load_area_scene(
    PreviewRenderResources& resources, std::string_view area_resref, PreviewSceneLoadOptions options);
void refresh_scene_local_light_render_data(PreviewScene& scene);
bool refresh_scene_dynamic_local_light_render_data(PreviewScene& scene);
PreviewLoadReport build_preview_load_report(std::string_view source, std::string_view animation_name = {});
PreviewRuntimeModelReports build_preview_runtime_model_reports(
    const PreviewScene& scene,
    const nw::render::PreparedModelSurfaceDrawList* prepared_surfaces = nullptr);
Lighting studio_preview_lighting();
LightingSpace studio_preview_lighting_space() noexcept;
int32_t compute_particle_prime_ms(const PreviewScene& scene, bool explicit_animation);
std::string_view preferred_model_animation_name(
    const nw::model::Mdl& mdl, PreferredModelAnimationContext context);
std::string_view preview_load_resource_status_label(PreviewLoadResourceStatus status) noexcept;
std::string_view preview_load_event_severity_label(PreviewLoadEventSeverity severity) noexcept;

} // namespace nw::render::viewer
