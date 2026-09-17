#pragma once

#include "renderer.hpp"

#include <nw/render/forward_plus_debug_mode.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Rml {
class ElementDocument;
class Context;
}

namespace nw::toolset {

// One application's HUD history. Renderer snapshots are borrowed only during
// update; this initial extraction preserves the baseline counter/timing fields.
Rml::ElementDocument* load_viewer_fps_document(Rml::Context& context);

struct ClientMetricsState {
    float viewer_fps_frame_seconds = 0.0f;
    float viewer_fps_smoothed_seconds = 0.0f;
    float viewer_fps_work_seconds = 0.0f;
    float viewer_fps_work_smoothed_seconds = 0.0f;
    float viewer_fps_sync_seconds = 0.0f;
    float viewer_fps_sync_smoothed_seconds = 0.0f;
    float viewer_fps_draw_seconds = 0.0f;
    float viewer_fps_draw_smoothed_seconds = 0.0f;
    float viewer_fps_ui_seconds = 0.0f;
    float viewer_fps_ui_smoothed_seconds = 0.0f;
    float viewer_fps_view_seconds = 0.0f;
    float viewer_fps_view_smoothed_seconds = 0.0f;
    float viewer_fps_hud_seconds = 0.0f;
    float viewer_fps_hud_smoothed_seconds = 0.0f;
    float viewer_fps_overlay_seconds = 0.0f;
    float viewer_fps_overlay_smoothed_seconds = 0.0f;
    float viewer_fps_palette_seconds = 0.0f;
    float viewer_fps_palette_smoothed_seconds = 0.0f;
    float viewer_fps_present_seconds = 0.0f;
    float viewer_fps_present_smoothed_seconds = 0.0f;
    float viewer_fps_tick_seconds = 0.0f;
    float viewer_fps_tick_smoothed_seconds = 0.0f;
    float viewer_fps_setup_seconds = 0.0f;
    float viewer_fps_setup_smoothed_seconds = 0.0f;
    float viewer_fps_shadow_seconds = 0.0f;
    float viewer_fps_shadow_smoothed_seconds = 0.0f;
    float viewer_fps_opaque_seconds = 0.0f;
    float viewer_fps_opaque_smoothed_seconds = 0.0f;
    float viewer_fps_water_seconds = 0.0f;
    float viewer_fps_water_smoothed_seconds = 0.0f;
    float viewer_fps_transparent_seconds = 0.0f;
    float viewer_fps_transparent_smoothed_seconds = 0.0f;
    float viewer_fps_particles_seconds = 0.0f;
    float viewer_fps_particles_smoothed_seconds = 0.0f;
    float viewer_fps_debug_seconds = 0.0f;
    float viewer_fps_debug_smoothed_seconds = 0.0f;
    float viewer_fps_area_prepare_seconds = 0.0f;
    float viewer_fps_area_prepare_smoothed_seconds = 0.0f;
    float viewer_fps_view_internal_seconds = 0.0f;
    float viewer_fps_view_internal_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_shadow_seconds = 0.0f;
    float viewer_fps_gpu_shadow_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_opaque_seconds = 0.0f;
    float viewer_fps_gpu_opaque_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_water_seconds = 0.0f;
    float viewer_fps_gpu_water_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_transparent_seconds = 0.0f;
    float viewer_fps_gpu_transparent_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_particles_seconds = 0.0f;
    float viewer_fps_gpu_particles_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_debug_seconds = 0.0f;
    float viewer_fps_gpu_debug_smoothed_seconds = 0.0f;
    float viewer_fps_gpu_total_seconds = 0.0f;
    float viewer_fps_gpu_total_smoothed_seconds = 0.0f;
    uint32_t viewer_fps_gpu_timer_count = 0;
    float viewer_fps_editor_gpu_ui_seconds = 0.0f;
    float viewer_fps_editor_gpu_ui_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_viewport_seconds = 0.0f;
    float viewer_fps_editor_gpu_viewport_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_overlay_seconds = 0.0f;
    float viewer_fps_editor_gpu_overlay_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_palette_seconds = 0.0f;
    float viewer_fps_editor_gpu_palette_smoothed_seconds = 0.0f;
    float viewer_fps_editor_gpu_total_seconds = 0.0f;
    float viewer_fps_editor_gpu_total_smoothed_seconds = 0.0f;
    uint32_t viewer_fps_editor_gpu_timer_count = 0;
    uint32_t viewer_fps_model_count = 0;
    uint32_t viewer_fps_particle_system_count = 0;
    size_t viewer_fps_render_model_animation_sample_input_count = 0;
    size_t viewer_fps_render_model_animation_sampled_count = 0;
    size_t viewer_fps_render_model_animation_disabled_count = 0;
    size_t viewer_fps_render_model_animation_missing_asset_data_count = 0;
    size_t viewer_fps_render_model_animation_invalid_skeleton_count = 0;
    size_t viewer_fps_render_model_animation_failed_sample_count = 0;
    uint32_t viewer_fps_prepared_model_surface_draw_count = 0;
    uint32_t viewer_fps_prepared_model_surface_render_model_draw_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_skinned_surface_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_assigned_surface_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_entry_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_matrix_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_bind_pose_fallback_count = 0;
    uint32_t viewer_fps_prepared_render_model_skin_table_invalid_skin_index_count = 0;
    uint32_t viewer_fps_area_cache_record_count = 0;
    uint32_t viewer_fps_area_cache_static_record_count = 0;
    uint32_t viewer_fps_area_cache_dynamic_record_count = 0;
    uint32_t viewer_fps_area_cache_opaque_record_count = 0;
    uint32_t viewer_fps_area_cache_water_record_count = 0;
    uint32_t viewer_fps_area_cache_transparent_record_count = 0;
    uint32_t viewer_fps_area_cache_shadow_caster_record_count = 0;
    uint32_t viewer_fps_area_cache_prepared_draw_count = 0;
    uint32_t viewer_fps_area_cache_light_index_count = 0;
    uint32_t viewer_fps_area_cache_max_light_indices_per_record = 0;
    uint32_t viewer_fps_area_cache_chunk_count = 0;
    uint32_t viewer_fps_area_cache_nonempty_chunk_count = 0;
    uint32_t viewer_fps_area_cache_max_records_per_chunk = 0;
    uint32_t viewer_fps_area_frame_visible_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_static_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_dynamic_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_chunk_count = 0;
    uint32_t viewer_fps_area_frame_opaque_record_count = 0;
    uint32_t viewer_fps_area_frame_water_record_count = 0;
    uint32_t viewer_fps_area_frame_transparent_record_count = 0;
    uint32_t viewer_fps_area_frame_shadow_caster_record_count = 0;
    uint32_t viewer_fps_area_frame_visible_prepared_surface_count = 0;
    bool viewer_fps_area_frame_uses_cached_draw_lists = false;
    uint32_t viewer_fps_local_light_count = 0;
    uint32_t viewer_fps_local_light_colored_count = 0;
    float viewer_fps_local_light_color_max = 0.0f;
    float viewer_fps_local_light_intensity_max = 0.0f;
    uint32_t viewer_fps_local_light_selected_draw_count = 0;
    uint32_t viewer_fps_local_light_selected_total = 0;
    uint32_t viewer_fps_local_light_selected_max = 0;
    uint32_t viewer_fps_local_light_selected_colored_total = 0;
    float viewer_fps_local_light_selected_color_max = 0.0f;
    float viewer_fps_local_light_selected_intensity_max = 0.0f;
    uint32_t viewer_fps_forward_plus_light_count = 0;
    uint32_t viewer_fps_forward_plus_cluster_count = 0;
    uint32_t viewer_fps_forward_plus_active_cluster_count = 0;
    uint32_t viewer_fps_forward_plus_cluster_light_index_count = 0;
    uint32_t viewer_fps_forward_plus_max_lights_per_cluster = 0;
    uint32_t viewer_fps_forward_plus_overflow_cluster_count = 0;
    uint32_t viewer_fps_forward_plus_overflow_light_count = 0;
    uint32_t viewer_fps_forward_plus_upload_bytes = 0;
    uint32_t viewer_fps_forward_plus_tile_size = 0;
    uint32_t viewer_fps_forward_plus_depth_slices = 0;
    uint32_t viewer_fps_shadow_cascade_count = 0;
    uint32_t viewer_fps_shadow_resolution = 0;
    uint32_t viewer_fps_shadow_caster_model_count = 0;
    uint32_t viewer_fps_shadow_no_caster_model_count = 0;
    uint32_t viewer_fps_shadow_submitted_model_count = 0;
    uint32_t viewer_fps_shadow_culled_model_count = 0;
    uint32_t viewer_fps_main_pass_count = 0;
    uint64_t viewer_fps_draw_count = 0;
    uint64_t viewer_fps_shadow_draw_count = 0;
    uint64_t viewer_fps_transparent_draw_count = 0;
    uint64_t viewer_fps_particle_draw_count = 0;
    uint64_t viewer_fps_indirect_draw_call_count = 0;
    uint64_t viewer_fps_draw_instance_count = 0;
    uint64_t viewer_fps_draw_index_count = 0;
    uint64_t viewer_fps_pipeline_bind_count = 0;
    uint64_t viewer_fps_pipeline_bind_skipped_count = 0;
    uint64_t viewer_fps_resource_bind_count = 0;
    uint64_t viewer_fps_resource_bind_skipped_count = 0;
    uint64_t viewer_fps_uniform_allocation_count = 0;
    uint64_t viewer_fps_uniform_allocation_bytes = 0;
    uint64_t viewer_fps_descriptor_allocation_failure_count = 0;
    uint64_t viewer_fps_descriptor_ring_capacity_bytes = 0;
    uint64_t viewer_fps_descriptor_ring_required_bytes = 0;
    uint64_t viewer_fps_resource_bind_failure_count = 0;
    uint64_t viewer_fps_dropped_draw_count = 0;
    bool viewer_fps_shadows_rendered = false;
    bool viewer_fps_water_rendered = false;
};

// The renderer is the process's shared GPU resource. This scope borrows it
// synchronously; an array index cannot replace the renderer's timer API.
class ScopedClientGpuTimer {
public:
    ScopedClientGpuTimer(ClientRenderer& renderer, const char* label)
        : renderer_{&renderer}
        , scope_{renderer.begin_gpu_timer(label)}
    {
    }

    ~ScopedClientGpuTimer()
    {
        if (renderer_ && scope_.valid()) {
            renderer_->end_gpu_timer(scope_);
        }
    }

    ScopedClientGpuTimer(const ScopedClientGpuTimer&) = delete;
    ScopedClientGpuTimer& operator=(const ScopedClientGpuTimer&) = delete;

private:
    ClientRenderer* renderer_ = nullptr;
    ClientGpuTimerScope scope_{};
};

void smooth_viewer_metric(float& latest_seconds, float& smoothed_seconds, float sample_seconds);
void update_viewer_frame_metrics(ClientMetricsState& state, float frame_seconds);
void update_viewer_render_metrics(ClientMetricsState& state,
    float work_seconds, float sync_seconds, float draw_seconds,
    float ui_seconds, float view_seconds, float hud_seconds,
    float overlay_seconds, float palette_seconds, float present_seconds);
void update_viewer_internal_metrics(ClientMetricsState& state,
    const nw::render::viewer::ViewerFrameStats* stats);
void update_client_gpu_metrics(ClientMetricsState& state, const ClientGpuFrameStats* stats);
[[nodiscard]] std::string format_viewer_fps_rml(const ClientMetricsState& state,
    bool forward_plus_enabled, nw::render::ForwardPlusDebugMode debug_mode);
void sync_viewer_fps_overlay(Rml::ElementDocument* fps_doc,
    ClientViewportRect rect, const ClientMetricsState& state,
    bool forward_plus_enabled, nw::render::ForwardPlusDebugMode debug_mode);

} // namespace nw::toolset
