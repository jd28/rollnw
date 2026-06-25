#pragma once

#include "camera.hpp"
#include "forward_plus.hpp"
#include "preview_model_draws.hpp"
#include "preview_render_resources.hpp"
#include "preview_scene.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/render/model_instance_animation.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw::render::viewer {

class SceneDebugRenderer;

struct ViewerViewport {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

enum class ViewerSceneKind : uint8_t {
    none,
    model,
    area,
    object_file,
};

struct ViewerFrameStats {
    float tick_seconds = 0.0f;
    float setup_seconds = 0.0f;
    float shadow_seconds = 0.0f;
    float opaque_seconds = 0.0f;
    float water_seconds = 0.0f;
    float transparent_seconds = 0.0f;
    float particles_seconds = 0.0f;
    float debug_seconds = 0.0f;
    float area_prepare_seconds = 0.0f;
    float local_shadow_seconds = 0.0f;
    float forward_plus_prepare_seconds = 0.0f;
    float forward_plus_light_cull_seconds = 0.0f;
    float forward_plus_cluster_header_seconds = 0.0f;
    float forward_plus_cluster_index_seconds = 0.0f;
    float forward_plus_upload_seconds = 0.0f;
    float total_render_seconds = 0.0f;
    float gpu_shadow_seconds = 0.0f;
    float gpu_depth_prepass_seconds = 0.0f;
    float gpu_opaque_seconds = 0.0f;
    float gpu_water_seconds = 0.0f;
    float gpu_transparent_seconds = 0.0f;
    float gpu_particles_seconds = 0.0f;
    float gpu_debug_seconds = 0.0f;
    float gpu_forward_plus_cull_seconds = 0.0f;
    uint32_t gpu_timer_count = 0;
    uint32_t model_count = 0;
    uint32_t static_model_count = 0;
    uint32_t particle_system_count = 0;
    PreviewSceneRuntimeSyncStats runtime_sync_stats{};
    nw::render::ModelInstanceAnimationSampleStats render_model_animation_sample_stats{};
    bool prepared_model_draw_validation_enabled = false;
    PreviewPreparedModelDrawValidation prepared_model_draw_validation{};
    bool prepared_render_model_draws_enabled = false;
    uint32_t prepared_render_model_draw_count = 0;
    uint32_t prepared_model_draw_material_fallback_count = 0;
    uint32_t prepared_model_draw_render_model_material_fallback_count = 0;
    uint32_t prepared_model_draw_nwn_legacy_material_fallback_count = 0;
    bool prepared_nwn_legacy_draws_enabled = false;
    uint32_t prepared_nwn_legacy_draw_count = 0;
    uint32_t prepared_nwn_legacy_selected_draw_count = 0;
    uint32_t prepared_nwn_legacy_missing_sidecar_draw_count = 0;
    uint32_t prepared_nwn_legacy_invalid_sidecar_draw_count = 0;
    bool prepared_model_surface_stats_enabled = false;
    nw::render::PreparedModelSurfaceDrawStats prepared_model_surface_stats{};
    nw::render::PreparedModelSurfaceMaterialStats prepared_model_surface_materials{};
    nw::render::PreparedModelSurfaceMaterialBindingStats prepared_model_surface_material_bindings{};
    nw::render::PreparedRenderModelSkinTableStats prepared_render_model_skin_table_stats{};
    nw::render::PreparedRenderModelSurfaceSubmissionStats prepared_render_model_surface_submission{};
    uint32_t particle_submitted_system_count = 0;
    uint32_t particle_culled_system_count = 0;
    uint32_t particle_invalid_material_packet_count = 0;
    uint32_t particle_mesh_packet_count = 0;
    uint32_t particle_mesh_particle_count = 0;
    uint32_t particle_mesh_submitted_particle_count = 0;
    uint32_t particle_mesh_dropped_particle_count = 0;
    uint32_t particle_mesh_missing_resref_packet_count = 0;
    uint32_t particle_mesh_missing_model_packet_count = 0;
    uint32_t particle_mesh_invalid_particle_index_count = 0;
    uint32_t area_cache_record_count = 0;
    uint32_t area_cache_static_record_count = 0;
    uint32_t area_cache_dynamic_record_count = 0;
    uint32_t area_cache_opaque_record_count = 0;
    uint32_t area_cache_water_record_count = 0;
    uint32_t area_cache_transparent_record_count = 0;
    uint32_t area_cache_shadow_caster_record_count = 0;
    uint32_t area_cache_prepared_draw_count = 0;
    uint32_t area_cache_shadow_prepared_surface_count = 0;
    uint32_t area_cache_static_geometry_mesh_count = 0;
    uint32_t area_cache_static_geometry_vertex_count = 0;
    uint32_t area_cache_static_geometry_index_count = 0;
    uint32_t area_cache_static_geometry_bytes = 0;
    uint32_t area_cache_light_index_count = 0;
    uint32_t area_cache_local_light_count = 0;
    uint32_t area_cache_max_light_indices_per_record = 0;
    uint32_t area_cache_max_shadow_prepared_surfaces_per_record = 0;
    uint32_t area_cache_chunk_light_index_count = 0;
    uint32_t area_cache_max_light_indices_per_chunk = 0;
    uint32_t area_cache_chunk_count = 0;
    uint32_t area_cache_nonempty_chunk_count = 0;
    uint32_t area_cache_max_records_per_chunk = 0;
    uint32_t area_frame_visible_record_count = 0;
    uint32_t area_frame_visible_static_record_count = 0;
    uint32_t area_frame_visible_dynamic_record_count = 0;
    uint32_t area_frame_visible_chunk_count = 0;
    uint32_t area_frame_culled_record_count = 0;
    uint32_t area_frame_culled_chunk_count = 0;
    uint32_t area_frame_visibility_culled_record_count = 0;
    uint32_t area_frame_visibility_culled_chunk_count = 0;
    uint32_t area_frame_opaque_record_count = 0;
    uint32_t area_frame_water_record_count = 0;
    uint32_t area_frame_transparent_record_count = 0;
    uint32_t area_frame_shadow_caster_record_count = 0;
    uint32_t area_frame_visible_prepared_surface_count = 0;
    uint32_t area_frame_visible_light_count = 0;
    bool area_frame_uses_cached_draw_lists = false;
    bool area_frame_uses_sorted_static_draw_lists = false;
    AreaPreparedSurfaceSidecarStats area_frame_material_indirect_sidecar_bridge{};
    AreaPreparedSurfaceSidecarStats area_frame_static_material_indirect_sidecar_bridge{};
    AreaPreparedSurfaceSidecarStats area_static_material_draw_data_sidecar_bridge{};
    AreaPreparedSurfaceSidecarStats area_static_material_sidecar_submission{};
    AreaDirectModelSubmissionStats area_direct_model_submission{};
    uint32_t local_light_count = 0;
    uint32_t local_light_authored_model_count = 0;
    uint32_t local_light_tile_model_count = 0;
    uint32_t local_light_placeable_table_count = 0;
    uint32_t local_light_colored_count = 0;
    float local_light_color_max = 0.0f;
    float local_light_intensity_max = 0.0f;
    uint32_t local_light_selected_draw_count = 0;
    uint32_t local_light_selected_total = 0;
    uint32_t local_light_selected_max = 0;
    uint32_t local_light_selected_colored_total = 0;
    float local_light_selected_color_max = 0.0f;
    float local_light_selected_intensity_max = 0.0f;
    bool forward_plus_prepared = false;
    bool forward_plus_active = false;
    bool forward_plus_empty = false;
    bool forward_plus_gpu_culling = false;
    uint32_t forward_plus_light_count = 0;
    uint32_t forward_plus_cluster_count = 0;
    uint32_t forward_plus_active_cluster_count = 0;
    uint32_t forward_plus_cluster_light_index_count = 0;
    uint32_t forward_plus_cluster_light_index_capacity = 0;
    uint32_t forward_plus_max_lights_per_cluster = 0;
    uint32_t forward_plus_overflow_cluster_count = 0;
    uint32_t forward_plus_overflow_light_count = 0;
    uint32_t forward_plus_upload_bytes = 0;
    uint32_t forward_plus_tile_size = 0;
    uint32_t forward_plus_depth_slices = 0;
    uint32_t shadow_cascade_count = 0;
    uint32_t shadow_resolution = 0;
    uint32_t shadow_caster_model_count = 0;
    uint32_t shadow_no_caster_model_count = 0;
    uint32_t shadow_submitted_model_count = 0;
    uint32_t shadow_culled_model_count = 0;
    uint32_t shadow_prepared_surface_shadow_range_count = 0;
    uint32_t shadow_prepared_surface_invalid_range_count = 0;
    AreaPreparedSurfaceSidecarStats shadow_area_indirect_sidecar_bridge{};
    AreaPreparedSurfaceSidecarStats shadow_area_sidecar_bridge{};
    uint32_t local_shadow_caster_light_count = 0;
    uint32_t local_shadow_submitted_model_count = 0;
    uint32_t local_shadow_culled_model_count = 0;
    AreaPreparedSurfaceSidecarStats local_shadow_area_sidecar_bridge{};
    uint32_t static_batch_material_batch_count = 0;
    uint32_t static_batch_material_input_draw_count = 0;
    uint32_t static_batch_material_instance_count = 0;
    uint32_t static_batch_material_draw_call_count = 0;
    uint32_t static_batch_material_fallback_draw_count = 0;
    uint32_t static_batch_material_failed_batch_attempt_draw_count = 0;
    uint32_t static_batch_material_indirect_call_count = 0;
    uint32_t static_batch_material_indirect_command_count = 0;
    uint32_t static_batch_material_max_instances_per_draw = 0;
    uint64_t static_batch_material_indirect_command_upload_bytes = 0;
    uint64_t static_batch_material_cached_indirect_command_bytes = 0;
    uint64_t static_batch_material_draw_data_bytes = 0;
    uint32_t static_batch_material_draw_data_cache_hit_count = 0;
    uint64_t static_batch_material_cached_draw_data_bytes = 0;
    uint32_t static_batch_shadow_batch_count = 0;
    uint32_t static_batch_shadow_input_draw_count = 0;
    uint32_t static_batch_shadow_instance_count = 0;
    uint32_t static_batch_shadow_draw_call_count = 0;
    uint32_t static_batch_shadow_failed_batch_attempt_draw_count = 0;
    uint32_t static_batch_shadow_indirect_call_count = 0;
    uint32_t static_batch_shadow_indirect_command_count = 0;
    uint32_t static_batch_shadow_max_instances_per_draw = 0;
    uint64_t static_batch_shadow_indirect_command_upload_bytes = 0;
    uint64_t static_batch_shadow_cached_indirect_command_bytes = 0;
    uint64_t static_batch_shadow_draw_data_bytes = 0;
    uint32_t static_batch_shadow_draw_data_cache_hit_count = 0;
    uint64_t static_batch_shadow_cached_draw_data_bytes = 0;
    uint32_t main_pass_count = 0;
    nw::gfx::CommandStats total_command_stats{};
    nw::gfx::CommandStats shadow_command_stats{};
    nw::gfx::CommandStats local_shadow_command_stats{};
    nw::gfx::CommandStats opaque_command_stats{};
    nw::gfx::CommandStats water_command_stats{};
    nw::gfx::CommandStats transparent_command_stats{};
    nw::gfx::CommandStats particle_command_stats{};
    nw::gfx::CommandStats debug_command_stats{};
    bool shadows_rendered = false;
    bool local_shadows_rendered = false;
    bool water_rendered = false;
};

class ViewerSession {
public:
    explicit ViewerSession(PreviewRenderResources& preview_resources, SceneDebugRenderer* debug_renderer = nullptr);
    ~ViewerSession();

    ViewerSession(const ViewerSession&) = delete;
    ViewerSession& operator=(const ViewerSession&) = delete;

    bool load_model(std::string_view resref);
    bool load_area(std::string_view resref);
    bool load_object_file(const std::filesystem::path& path);
    void clear();

    void tick(int32_t dt_ms);
    void render(nw::gfx::CommandList* command_list, ViewerViewport viewport);

    bool select_animation(std::string_view animation);
    bool fit_to_scene(ViewerViewport viewport);
    bool set_area_gameplay_view(ViewerViewport viewport, float fov_degrees = 65.0f);
    void update_viewport(ViewerViewport viewport);

    [[nodiscard]] Camera& camera() noexcept { return camera_; }
    [[nodiscard]] const Camera& camera() const noexcept { return camera_; }
    [[nodiscard]] PreviewScene* scene() noexcept { return scene_.get(); }
    [[nodiscard]] const PreviewScene* scene() const noexcept { return scene_.get(); }
    [[nodiscard]] ViewerSceneKind scene_kind() const noexcept { return scene_kind_; }
    [[nodiscard]] std::string_view loaded_source() const noexcept { return loaded_source_; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    void set_playing(bool playing) noexcept { playing_ = playing; }
    [[nodiscard]] PreviewSceneLoadOptions preview_scene_load_options() const noexcept
    {
        return preview_scene_load_options_;
    }
    void set_preview_scene_load_options(PreviewSceneLoadOptions options) noexcept
    {
        preview_scene_load_options_ = options;
    }
    [[nodiscard]] bool area_day_night_autoplay() const noexcept { return area_day_night_autoplay_; }
    void set_area_day_night_autoplay(bool enabled) noexcept { area_day_night_autoplay_ = enabled; }
    [[nodiscard]] float area_day_night_elapsed_seconds() const noexcept { return area_day_night_elapsed_seconds_; }
    void set_area_day_night_elapsed_seconds(float elapsed_seconds, bool log_transition = false);
    void reset_area_day_night_cycle();
    [[nodiscard]] bool authored_area_fog_enabled() const noexcept { return authored_area_fog_enabled_; }
    void set_authored_area_fog_enabled(bool enabled) noexcept { authored_area_fog_enabled_ = enabled; }
    [[nodiscard]] bool area_lights_enabled() const noexcept { return area_lights_enabled_; }
    void set_area_lights_enabled(bool enabled) noexcept { area_lights_enabled_ = enabled; }
    [[nodiscard]] bool area_debug_enabled() const noexcept { return area_debug_enabled_; }
    void set_area_debug_enabled(bool enabled) noexcept { area_debug_enabled_ = enabled; }
    [[nodiscard]] bool area_triggers_enabled() const noexcept { return area_triggers_enabled_; }
    void set_area_triggers_enabled(bool enabled) noexcept { area_triggers_enabled_ = enabled; }
    [[nodiscard]] bool area_encounters_enabled() const noexcept { return area_encounters_enabled_; }
    void set_area_encounters_enabled(bool enabled) noexcept { area_encounters_enabled_ = enabled; }
    [[nodiscard]] bool area_shadows_enabled() const noexcept { return area_shadows_enabled_; }
    void set_area_shadows_enabled(bool enabled) noexcept { area_shadows_enabled_ = enabled; }
    [[nodiscard]] bool local_shadows_enabled() const noexcept { return local_shadows_enabled_; }
    void set_local_shadows_enabled(bool enabled) noexcept { local_shadows_enabled_ = enabled; }
    void set_area_visibility_mask(std::span<const uint8_t> visible_chunk_mask);
    void set_area_visibility_radius_tiles(int32_t radius_tiles);
    void set_area_visibility_view_cone_tiles(int32_t radius_tiles, float half_angle_degrees = 75.0f);
    void clear_area_visibility_mask();
    [[nodiscard]] size_t area_visibility_mask_visible_chunk_count() const noexcept
    {
        return area_visibility_mask_visible_chunk_count_;
    }
    [[nodiscard]] uint32_t shadow_debug_mode() const noexcept { return shadow_debug_mode_; }
    void set_shadow_debug_mode(uint32_t mode) noexcept { shadow_debug_mode_ = mode; }
    void set_static_pbr_environment(
        float ibl_strength,
        float exposure,
        uint32_t ibl_diffuse_texture_index,
        uint32_t ibl_specular_texture_index,
        uint32_t brdf_lut_texture_index,
        bool enabled) noexcept;
    [[nodiscard]] const ForwardPlusRenderPolicy& forward_plus_policy() const noexcept
    {
        return forward_plus_policy_;
    }
    void set_forward_plus_policy(const ForwardPlusRenderPolicy& policy) noexcept
    {
        forward_plus_policy_ = policy;
    }
    [[nodiscard]] bool forward_plus_enabled() const noexcept
    {
        return forward_plus_policy_.enabled;
    }
    void set_forward_plus_enabled(bool enabled) noexcept
    {
        forward_plus_policy_.enabled = enabled;
    }
    [[nodiscard]] nw::render::ForwardPlusDebugMode forward_plus_debug_mode() const noexcept
    {
        return forward_plus_policy_.debug_mode;
    }
    void set_forward_plus_debug_mode(nw::render::ForwardPlusDebugMode mode) noexcept
    {
        forward_plus_policy_.debug_mode = mode;
    }
    [[nodiscard]] const ViewerFrameStats& last_frame_stats() const noexcept { return last_frame_stats_; }

private:
    bool set_scene(std::unique_ptr<PreviewScene> scene, ViewerSceneKind kind, std::string source);
    void fit_loaded_scene();
    [[nodiscard]] nw::render::RenderContext make_render_context() const;

    PreviewRenderResources* preview_resources_ = nullptr;
    SceneDebugRenderer* debug_renderer_ = nullptr;
    Camera camera_;
    std::unique_ptr<PreviewScene> scene_;
    ViewerViewport viewport_;
    ViewerSceneKind scene_kind_ = ViewerSceneKind::none;
    std::string loaded_source_;
    float scene_time_seconds_ = 0.0f;
    float area_day_night_elapsed_seconds_ = 0.0f;
    float last_tick_seconds_ = 0.0f;
    float static_pbr_ibl_strength_ = 1.0f;
    float static_pbr_exposure_ = 1.0f;
    uint32_t static_pbr_ibl_diffuse_texture_index_ = 0;
    uint32_t static_pbr_ibl_specular_texture_index_ = 0;
    uint32_t static_pbr_brdf_lut_texture_index_ = 0;
    uint32_t shadow_debug_mode_ = 0;
    ForwardPlusRenderPolicy forward_plus_policy_{};
    ViewerFrameStats last_frame_stats_{};
    AreaRenderFrame area_frame_;
    ForwardPlusFrame forward_plus_frame_;
    PreviewPreparedModelDraws prepared_model_draws_;
    nw::render::PreparedModelSurfaceDrawList prepared_model_surfaces_;
    std::vector<nw::render::ModelInstanceHandle> area_visible_model_instance_handles_;
    PreviewSceneLoadOptions preview_scene_load_options_{};
    // Reused per-frame sampling scratch. Runtime animation state lives on
    // ModelInstance; this vector must not own clip, time, pose, or skin state.
    std::vector<nw::render::ModelInstanceAnimationSample> render_model_animation_samples_;
    std::vector<const nw::render::nwn::PreparedDrawItem*> nwn_prepared_draw_items_;
    nw::render::nwn::PreparedDrawScratch prepared_draw_scratch_;
    std::vector<nw::gfx::GpuTimerResult> completed_gpu_timer_results_;
    std::vector<uint8_t> area_visibility_mask_;
    size_t area_visibility_mask_visible_chunk_count_ = 0;
    int32_t area_visibility_radius_tiles_ = -1;
    float area_visibility_half_angle_degrees_ = 75.0f;
    AreaVisibilityMaskMode area_visibility_mask_mode_ = AreaVisibilityMaskMode::radius;
    bool area_day_night_autoplay_ = true;
    bool authored_area_fog_enabled_ = false;
    bool area_lights_enabled_ = true;
    bool area_debug_enabled_ = true;
    bool area_triggers_enabled_ = true;
    bool area_encounters_enabled_ = true;
    bool area_shadows_enabled_ = true;
    bool local_shadows_enabled_ = true;
    bool area_visibility_mask_enabled_ = false;
    bool static_pbr_ibl_enabled_ = false;
    bool playing_ = true;
};

} // namespace nw::render::viewer
