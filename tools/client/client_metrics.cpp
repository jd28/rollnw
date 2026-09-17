#include "client_metrics.hpp"
#include "forward_plus_debug.hpp"

#include <nw/render/viewer/session.hpp>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace nw::toolset {
namespace {

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return false;
    }

    std::string normalized{value};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized != "0" && normalized != "false" && normalized != "off" && normalized != "no";
}

} // namespace

Rml::ElementDocument* load_viewer_fps_document(Rml::Context& context)
{
    static constexpr const char* kFpsOverlayRml = R"RML(
<rml>
<head>
  <style>
    body {
      width: 100%;
      height: 100%;
      margin: 0px;
      padding: 0px;
      background: transparent;
      font-family: RollnwMono;
    }
    #viewer_fps_overlay {
      position: absolute;
      display: none;
      width: 430px;
      height: 54px;
      padding: 3px 7px;
      border: 1px #41505d;
      background: #101820;
      color: #e6eef3;
      font-family: RollnwMono;
      font-size: 11px;
      font-weight: normal;
      line-height: 15px;
      text-align: right;
    }
    #play_preview_viewport_overlay {
      position: absolute;
      display: none;
      width: 320px;
      height: 50px;
      padding: 7px 12px 8px 12px;
      border: 1px #9b835d;
      border-radius: 3px;
      background: #0d1217dd;
      pointer-events: none;
    }
    .play_preview_viewport_title {
      display: block;
      width: 100%;
      color: #eadcc3;
      font-size: 17px;
      font-weight: bold;
      line-height: 21px;
    }
    .play_preview_viewport_help {
      display: block;
      width: 100%;
      color: #aeb8c3;
      font-size: 11px;
      font-weight: normal;
      line-height: 14px;
    }
    .play_preview_viewport_error {
      display: block;
      width: 100%;
      color: #ff9a8a;
      font-size: 11px;
      font-weight: normal;
      line-height: 14px;
    }
  </style>
</head>
<body>
  <div id="viewer_fps_overlay">-- FPS</div>
  <div id="play_preview_viewport_overlay"></div>
</body>
</rml>
)RML";

    return context.LoadDocumentFromMemory(kFpsOverlayRml, "viewer_fps_overlay.rml");
}

void smooth_viewer_metric(float& latest_seconds, float& smoothed_seconds, float sample_seconds)
{
    if (sample_seconds < 0.0f) {
        return;
    }

    latest_seconds = sample_seconds;
    if (smoothed_seconds <= 0.0f) {
        smoothed_seconds = sample_seconds;
    } else {
        constexpr float kSmoothing = 0.10f;
        smoothed_seconds += (sample_seconds - smoothed_seconds) * kSmoothing;
    }
}

float display_metric_seconds(float latest_seconds, float smoothed_seconds)
{
    return smoothed_seconds > 0.0f ? smoothed_seconds : latest_seconds;
}

bool viewer_fps_overlay_verbose()
{
    static const bool enabled = environment_flag_enabled("ROLLNW_CLIENT_FPS_OVERLAY_VERBOSE");
    return enabled;
}

void update_viewer_frame_metrics(ClientMetricsState& state, float frame_seconds)
{
    if (frame_seconds <= 0.0f) {
        return;
    }

    smooth_viewer_metric(state.viewer_fps_frame_seconds, state.viewer_fps_smoothed_seconds, frame_seconds);
}

void update_viewer_render_metrics(ClientMetricsState& state,
    float work_seconds,
    float sync_seconds,
    float draw_seconds,
    float ui_seconds,
    float view_seconds,
    float hud_seconds,
    float overlay_seconds,
    float palette_seconds,
    float present_seconds)
{
    smooth_viewer_metric(state.viewer_fps_work_seconds, state.viewer_fps_work_smoothed_seconds, work_seconds);
    smooth_viewer_metric(state.viewer_fps_sync_seconds, state.viewer_fps_sync_smoothed_seconds, sync_seconds);
    smooth_viewer_metric(state.viewer_fps_draw_seconds, state.viewer_fps_draw_smoothed_seconds, draw_seconds);
    smooth_viewer_metric(state.viewer_fps_ui_seconds, state.viewer_fps_ui_smoothed_seconds, ui_seconds);
    smooth_viewer_metric(state.viewer_fps_view_seconds, state.viewer_fps_view_smoothed_seconds, view_seconds);
    smooth_viewer_metric(state.viewer_fps_hud_seconds, state.viewer_fps_hud_smoothed_seconds, hud_seconds);
    smooth_viewer_metric(state.viewer_fps_overlay_seconds, state.viewer_fps_overlay_smoothed_seconds, overlay_seconds);
    smooth_viewer_metric(state.viewer_fps_palette_seconds, state.viewer_fps_palette_smoothed_seconds, palette_seconds);
    smooth_viewer_metric(state.viewer_fps_present_seconds, state.viewer_fps_present_smoothed_seconds, present_seconds);
}

void update_viewer_internal_metrics(ClientMetricsState& state, const nw::render::viewer::ViewerFrameStats* stats)
{
    if (!stats) {
        return;
    }

    state.viewer_stats = *stats;

    smooth_viewer_metric(state.viewer_fps_tick_seconds, state.viewer_fps_tick_smoothed_seconds, stats->tick_seconds);
    smooth_viewer_metric(state.viewer_fps_setup_seconds, state.viewer_fps_setup_smoothed_seconds, stats->setup_seconds);
    smooth_viewer_metric(state.viewer_fps_shadow_seconds, state.viewer_fps_shadow_smoothed_seconds, stats->shadow_seconds);
    smooth_viewer_metric(state.viewer_fps_opaque_seconds, state.viewer_fps_opaque_smoothed_seconds, stats->opaque_seconds);
    smooth_viewer_metric(state.viewer_fps_water_seconds, state.viewer_fps_water_smoothed_seconds, stats->water_seconds);
    smooth_viewer_metric(state.viewer_fps_transparent_seconds,
        state.viewer_fps_transparent_smoothed_seconds,
        stats->transparent_seconds);
    smooth_viewer_metric(state.viewer_fps_particles_seconds,
        state.viewer_fps_particles_smoothed_seconds,
        stats->particles_seconds);
    smooth_viewer_metric(state.viewer_fps_debug_seconds,
        state.viewer_fps_debug_smoothed_seconds,
        stats->debug_seconds);
    smooth_viewer_metric(state.viewer_fps_area_prepare_seconds,
        state.viewer_fps_area_prepare_smoothed_seconds,
        stats->area_prepare_seconds);
    smooth_viewer_metric(state.viewer_fps_view_internal_seconds,
        state.viewer_fps_view_internal_smoothed_seconds,
        stats->total_render_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_shadow_seconds,
        state.viewer_fps_gpu_shadow_smoothed_seconds,
        stats->gpu_shadow_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_opaque_seconds,
        state.viewer_fps_gpu_opaque_smoothed_seconds,
        stats->gpu_opaque_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_water_seconds,
        state.viewer_fps_gpu_water_smoothed_seconds,
        stats->gpu_water_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_transparent_seconds,
        state.viewer_fps_gpu_transparent_smoothed_seconds,
        stats->gpu_transparent_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_particles_seconds,
        state.viewer_fps_gpu_particles_smoothed_seconds,
        stats->gpu_particles_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_debug_seconds,
        state.viewer_fps_gpu_debug_smoothed_seconds,
        stats->gpu_debug_seconds);
    smooth_viewer_metric(state.viewer_fps_gpu_total_seconds,
        state.viewer_fps_gpu_total_smoothed_seconds,
        stats->gpu_shadow_seconds + stats->gpu_opaque_seconds + stats->gpu_water_seconds
            + stats->gpu_transparent_seconds + stats->gpu_particles_seconds + stats->gpu_debug_seconds);
}

void update_client_gpu_metrics(ClientMetricsState& state, const ClientGpuFrameStats* stats)
{
    if (!stats) {
        return;
    }

    state.editor_gpu_stats = *stats;

    smooth_viewer_metric(state.viewer_fps_editor_gpu_ui_seconds,
        state.viewer_fps_editor_gpu_ui_smoothed_seconds,
        stats->ui_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_viewport_seconds,
        state.viewer_fps_editor_gpu_viewport_smoothed_seconds,
        stats->viewport_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_overlay_seconds,
        state.viewer_fps_editor_gpu_overlay_smoothed_seconds,
        stats->overlay_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_palette_seconds,
        state.viewer_fps_editor_gpu_palette_smoothed_seconds,
        stats->palette_seconds);
    smooth_viewer_metric(state.viewer_fps_editor_gpu_total_seconds,
        state.viewer_fps_editor_gpu_total_smoothed_seconds,
        stats->total_seconds);
}

std::string format_viewer_fps_rml(const ClientMetricsState& state,
    bool forward_plus_enabled, nw::render::ForwardPlusDebugMode debug_mode)
{
    const float frame_seconds = display_metric_seconds(
        state.viewer_fps_frame_seconds, state.viewer_fps_smoothed_seconds);
    if (frame_seconds <= 0.0f) {
        return "-- FPS";
    }

    const float work_seconds = display_metric_seconds(
        state.viewer_fps_work_seconds, state.viewer_fps_work_smoothed_seconds);
    const float sync_seconds = display_metric_seconds(
        state.viewer_fps_sync_seconds, state.viewer_fps_sync_smoothed_seconds);
    const float draw_seconds = display_metric_seconds(
        state.viewer_fps_draw_seconds, state.viewer_fps_draw_smoothed_seconds);
    const float ui_seconds = display_metric_seconds(
        state.viewer_fps_ui_seconds, state.viewer_fps_ui_smoothed_seconds);
    const float view_seconds = display_metric_seconds(
        state.viewer_fps_view_seconds, state.viewer_fps_view_smoothed_seconds);
    const float hud_seconds = display_metric_seconds(
        state.viewer_fps_hud_seconds, state.viewer_fps_hud_smoothed_seconds);
    const float overlay_seconds = display_metric_seconds(
        state.viewer_fps_overlay_seconds, state.viewer_fps_overlay_smoothed_seconds);
    const float palette_seconds = display_metric_seconds(
        state.viewer_fps_palette_seconds, state.viewer_fps_palette_smoothed_seconds);
    const float present_seconds = display_metric_seconds(
        state.viewer_fps_present_seconds, state.viewer_fps_present_smoothed_seconds);
    const float tick_seconds = display_metric_seconds(
        state.viewer_fps_tick_seconds, state.viewer_fps_tick_smoothed_seconds);
    const float setup_seconds = display_metric_seconds(
        state.viewer_fps_setup_seconds, state.viewer_fps_setup_smoothed_seconds);
    const float shadow_seconds = display_metric_seconds(
        state.viewer_fps_shadow_seconds, state.viewer_fps_shadow_smoothed_seconds);
    const float opaque_seconds = display_metric_seconds(
        state.viewer_fps_opaque_seconds, state.viewer_fps_opaque_smoothed_seconds);
    const float water_seconds = display_metric_seconds(
        state.viewer_fps_water_seconds, state.viewer_fps_water_smoothed_seconds);
    const float transparent_seconds = display_metric_seconds(
        state.viewer_fps_transparent_seconds, state.viewer_fps_transparent_smoothed_seconds);
    const float particles_seconds = display_metric_seconds(
        state.viewer_fps_particles_seconds, state.viewer_fps_particles_smoothed_seconds);
    const float debug_seconds = display_metric_seconds(
        state.viewer_fps_debug_seconds, state.viewer_fps_debug_smoothed_seconds);
    const float area_prepare_seconds = display_metric_seconds(
        state.viewer_fps_area_prepare_seconds, state.viewer_fps_area_prepare_smoothed_seconds);
    const float view_internal_seconds = display_metric_seconds(
        state.viewer_fps_view_internal_seconds, state.viewer_fps_view_internal_smoothed_seconds);
    const float gpu_shadow_seconds = display_metric_seconds(
        state.viewer_fps_gpu_shadow_seconds, state.viewer_fps_gpu_shadow_smoothed_seconds);
    const float gpu_opaque_seconds = display_metric_seconds(
        state.viewer_fps_gpu_opaque_seconds, state.viewer_fps_gpu_opaque_smoothed_seconds);
    const float gpu_water_seconds = display_metric_seconds(
        state.viewer_fps_gpu_water_seconds, state.viewer_fps_gpu_water_smoothed_seconds);
    const float gpu_transparent_seconds = display_metric_seconds(
        state.viewer_fps_gpu_transparent_seconds, state.viewer_fps_gpu_transparent_smoothed_seconds);
    const float gpu_particles_seconds = display_metric_seconds(
        state.viewer_fps_gpu_particles_seconds, state.viewer_fps_gpu_particles_smoothed_seconds);
    const float gpu_debug_seconds = display_metric_seconds(
        state.viewer_fps_gpu_debug_seconds, state.viewer_fps_gpu_debug_smoothed_seconds);
    const float gpu_total_seconds = display_metric_seconds(
        state.viewer_fps_gpu_total_seconds, state.viewer_fps_gpu_total_smoothed_seconds);
    const float editor_gpu_ui_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_ui_seconds, state.viewer_fps_editor_gpu_ui_smoothed_seconds);
    const float editor_gpu_viewport_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_viewport_seconds, state.viewer_fps_editor_gpu_viewport_smoothed_seconds);
    const float editor_gpu_overlay_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_overlay_seconds, state.viewer_fps_editor_gpu_overlay_smoothed_seconds);
    const float editor_gpu_palette_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_palette_seconds, state.viewer_fps_editor_gpu_palette_smoothed_seconds);
    const float editor_gpu_total_seconds = display_metric_seconds(
        state.viewer_fps_editor_gpu_total_seconds, state.viewer_fps_editor_gpu_total_smoothed_seconds);

    char compact_frame_text[128]{};
    std::snprintf(compact_frame_text,
        sizeof(compact_frame_text),
        "%.1f FPS frame %.1f | view %.1f ui %.1f present %.1f ms",
        static_cast<double>(1.0f / frame_seconds),
        static_cast<double>(frame_seconds * 1000.0f),
        static_cast<double>(view_seconds * 1000.0f),
        static_cast<double>(ui_seconds * 1000.0f),
        static_cast<double>(present_seconds * 1000.0f));

    char compact_gpu_text[128]{};
    std::snprintf(compact_gpu_text,
        sizeof(compact_gpu_text),
        "gpu vp %.2f pass %.2f ui %.2f ov %.2f pal %.2f ms",
        static_cast<double>(editor_gpu_viewport_seconds * 1000.0f),
        static_cast<double>(gpu_total_seconds * 1000.0f),
        static_cast<double>(editor_gpu_ui_seconds * 1000.0f),
        static_cast<double>(editor_gpu_overlay_seconds * 1000.0f),
        static_cast<double>(editor_gpu_palette_seconds * 1000.0f));

    char compact_scene_text[128]{};
    std::snprintf(compact_scene_text,
        sizeof(compact_scene_text),
        "vis %u chunks %u lights %u | draws %llu ind %llu",
        state.viewer_stats.area_frame_visible_record_count,
        state.viewer_stats.area_frame_visible_chunk_count,
        state.viewer_stats.forward_plus_light_count,
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.draw_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.indirect_draw_call_count));

    std::string compact_result = Rml::StringUtilities::EncodeRml(compact_frame_text);
    if (state.viewer_stats.gpu_timer_count > 0 || state.editor_gpu_stats.timer_count > 0) {
        compact_result += "<br/>";
        compact_result += Rml::StringUtilities::EncodeRml(compact_gpu_text);
    }
    compact_result += "<br/>";
    compact_result += Rml::StringUtilities::EncodeRml(compact_scene_text);
    if (!viewer_fps_overlay_verbose()) {
        return compact_result;
    }

    char frame_text[64]{};
    std::snprintf(frame_text,
        sizeof(frame_text),
        "%.1f FPS | frame %.1f ms",
        static_cast<double>(1.0f / frame_seconds),
        static_cast<double>(frame_seconds * 1000.0f));

    char cost_text[128]{};
    std::snprintf(cost_text,
        sizeof(cost_text),
        "work %.1f sync %.1f cpu-draw %.1f present %.1f ms",
        static_cast<double>(work_seconds * 1000.0f),
        static_cast<double>(sync_seconds * 1000.0f),
        static_cast<double>(draw_seconds * 1000.0f),
        static_cast<double>(present_seconds * 1000.0f));

    char draw_text[144]{};
    std::snprintf(draw_text,
        sizeof(draw_text),
        "ui %.1f view %.1f hud %.1f overlay %.1f palette %.1f ms",
        static_cast<double>(ui_seconds * 1000.0f),
        static_cast<double>(view_seconds * 1000.0f),
        static_cast<double>(hud_seconds * 1000.0f),
        static_cast<double>(overlay_seconds * 1000.0f),
        static_cast<double>(palette_seconds * 1000.0f));

    char view_text[160]{};
    std::snprintf(view_text,
        sizeof(view_text),
        "view total %.1f tick %.1f setup %.1f prep %.3f shadow %.1f particles %.1f debug %.1f ms",
        static_cast<double>(view_internal_seconds * 1000.0f),
        static_cast<double>(tick_seconds * 1000.0f),
        static_cast<double>(setup_seconds * 1000.0f),
        static_cast<double>(area_prepare_seconds * 1000.0f),
        static_cast<double>(shadow_seconds * 1000.0f),
        static_cast<double>(particles_seconds * 1000.0f),
        static_cast<double>(debug_seconds * 1000.0f));

    char gpu_text[192]{};
    std::snprintf(gpu_text,
        sizeof(gpu_text),
        "gpu total %.2f opaque %.2f shadow %.2f water %.2f trans %.2f ps %.2f debug %.2f ms timers %u",
        static_cast<double>(gpu_total_seconds * 1000.0f),
        static_cast<double>(gpu_opaque_seconds * 1000.0f),
        static_cast<double>(gpu_shadow_seconds * 1000.0f),
        static_cast<double>(gpu_water_seconds * 1000.0f),
        static_cast<double>(gpu_transparent_seconds * 1000.0f),
        static_cast<double>(gpu_particles_seconds * 1000.0f),
        static_cast<double>(gpu_debug_seconds * 1000.0f),
        state.viewer_stats.gpu_timer_count);

    char editor_gpu_text[160]{};
    std::snprintf(editor_gpu_text,
        sizeof(editor_gpu_text),
        "gpu editor total %.2f ui %.2f viewport %.2f overlay %.2f palette %.2f ms timers %u",
        static_cast<double>(editor_gpu_total_seconds * 1000.0f),
        static_cast<double>(editor_gpu_ui_seconds * 1000.0f),
        static_cast<double>(editor_gpu_viewport_seconds * 1000.0f),
        static_cast<double>(editor_gpu_overlay_seconds * 1000.0f),
        static_cast<double>(editor_gpu_palette_seconds * 1000.0f),
        state.editor_gpu_stats.timer_count);

    char pass_text[192]{};
    std::snprintf(pass_text,
        sizeof(pass_text),
        "passes opaque %.1f water %.1f trans %.1f ms | models %u ps %u lights %u/%u c%.2f i%.2f lit %u/%u/%u lc%u c%.2f i%.2f sh %u pass %u",
        static_cast<double>(opaque_seconds * 1000.0f),
        static_cast<double>(water_seconds * 1000.0f),
        static_cast<double>(transparent_seconds * 1000.0f),
        state.viewer_stats.model_count,
        state.viewer_stats.particle_system_count,
        state.viewer_stats.local_light_count,
        state.viewer_stats.local_light_colored_count,
        static_cast<double>(state.viewer_stats.local_light_color_max),
        static_cast<double>(state.viewer_stats.local_light_intensity_max),
        state.viewer_stats.local_light_selected_draw_count,
        state.viewer_stats.local_light_selected_total,
        state.viewer_stats.local_light_selected_max,
        state.viewer_stats.local_light_selected_colored_total,
        static_cast<double>(state.viewer_stats.local_light_selected_color_max),
        static_cast<double>(state.viewer_stats.local_light_selected_intensity_max),
        state.viewer_stats.shadow_cascade_count,
        state.viewer_stats.main_pass_count);

    char shadow_text[144]{};
    std::snprintf(shadow_text,
        sizeof(shadow_text),
        "shadow res %u casters %u no-caster %u submitted %u culled %u",
        state.viewer_stats.shadow_resolution,
        state.viewer_stats.shadow_caster_model_count,
        state.viewer_stats.shadow_no_caster_model_count,
        state.viewer_stats.shadow_submitted_model_count,
        state.viewer_stats.shadow_culled_model_count);

    char render_model_text[256]{};
    std::snprintf(render_model_text,
        sizeof(render_model_text),
        "rmodel samples in %zu ok %zu dis %zu miss %zu badskel %zu fail %zu | surf %u rm %u skin %u assign %u entries %u mats %u bind %u invalid %u",
        state.viewer_stats.render_model_animation_sample_stats.input_count,
        state.viewer_stats.render_model_animation_sample_stats.sampled_count,
        state.viewer_stats.render_model_animation_sample_stats.disabled_count,
        state.viewer_stats.render_model_animation_sample_stats.missing_asset_data_count,
        state.viewer_stats.render_model_animation_sample_stats.invalid_skeleton_count,
        state.viewer_stats.render_model_animation_sample_stats.failed_sample_count,
        state.viewer_stats.prepared_model_surface_stats.draw_count,
        state.viewer_stats.prepared_model_surface_stats.render_model_draw_count,
        state.viewer_stats.prepared_render_model_skin_table_stats.render_model_skinned_surface_count,
        state.viewer_stats.prepared_render_model_skin_table_stats.assigned_surface_count,
        state.viewer_stats.prepared_render_model_skin_table_stats.table_entry_count,
        state.viewer_stats.prepared_render_model_skin_table_stats.matrix_count,
        state.viewer_stats.prepared_render_model_skin_table_stats.bind_pose_fallback_surface_count,
        state.viewer_stats.prepared_render_model_skin_table_stats.invalid_skin_index_count);

    char area_cache_text[224]{};
    std::snprintf(area_cache_text,
        sizeof(area_cache_text),
        "area cache rec %u static %u dyn %u prep draws %u lights %u max %u chunks %u/%u max %u pass %u/%u/%u sh %u",
        state.viewer_stats.area_cache_record_count,
        state.viewer_stats.area_cache_static_record_count,
        state.viewer_stats.area_cache_dynamic_record_count,
        state.viewer_stats.area_cache_prepared_draw_count,
        state.viewer_stats.area_cache_light_index_count,
        state.viewer_stats.area_cache_max_light_indices_per_record,
        state.viewer_stats.area_cache_nonempty_chunk_count,
        state.viewer_stats.area_cache_chunk_count,
        state.viewer_stats.area_cache_max_records_per_chunk,
        state.viewer_stats.area_cache_opaque_record_count,
        state.viewer_stats.area_cache_water_record_count,
        state.viewer_stats.area_cache_transparent_record_count,
        state.viewer_stats.area_cache_shadow_caster_record_count);

    char area_frame_text[224]{};
    std::snprintf(area_frame_text,
        sizeof(area_frame_text),
        "area frame vis %u static %u dyn %u prep surf %u chunks %u lists %u/%u/%u sh %u cached %u",
        state.viewer_stats.area_frame_visible_record_count,
        state.viewer_stats.area_frame_visible_static_record_count,
        state.viewer_stats.area_frame_visible_dynamic_record_count,
        state.viewer_stats.area_frame_visible_prepared_surface_count,
        state.viewer_stats.area_frame_visible_chunk_count,
        state.viewer_stats.area_frame_opaque_record_count,
        state.viewer_stats.area_frame_water_record_count,
        state.viewer_stats.area_frame_transparent_record_count,
        state.viewer_stats.area_frame_shadow_caster_record_count,
        state.viewer_stats.area_frame_uses_cached_draw_lists ? 1u : 0u);

    char forward_plus_text[192]{};
    std::snprintf(forward_plus_text,
        sizeof(forward_plus_text),
        "f+ %s lights %u clusters %u/%u refs %u max %u ov %u/%u upload %.1f KB tile %u z %u dbg %s",
        forward_plus_enabled ? "on" : "off",
        state.viewer_stats.forward_plus_light_count,
        state.viewer_stats.forward_plus_active_cluster_count,
        state.viewer_stats.forward_plus_cluster_count,
        state.viewer_stats.forward_plus_cluster_light_index_count,
        state.viewer_stats.forward_plus_max_lights_per_cluster,
        state.viewer_stats.forward_plus_overflow_cluster_count,
        state.viewer_stats.forward_plus_overflow_light_count,
        static_cast<double>(state.viewer_stats.forward_plus_upload_bytes) / 1024.0,
        state.viewer_stats.forward_plus_tile_size,
        state.viewer_stats.forward_plus_depth_slices,
        nw::toolset::forward_plus_debug_mode_label(debug_mode));

    char submit_text[224]{};
    std::snprintf(submit_text,
        sizeof(submit_text),
        "submit draws %llu ind %llu inst %llu idx %.1fM sh %llu trans %llu ps %llu | pipe %llu/%llu res %llu/%llu ubos %llu %.1f KB desc %.1f/%.1f KB fail %llu/%llu drop %llu",
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.draw_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.indirect_draw_call_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.draw_instance_count),
        static_cast<double>(state.viewer_stats.total_command_stats.draw_index_count) / 1000000.0,
        static_cast<unsigned long long>(state.viewer_stats.shadow_command_stats.draw_count),
        static_cast<unsigned long long>(state.viewer_stats.transparent_command_stats.draw_count),
        static_cast<unsigned long long>(state.viewer_stats.particle_command_stats.draw_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.pipeline_bind_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.pipeline_bind_skipped_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.resource_bind_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.resource_bind_skipped_count),
        static_cast<unsigned long long>(state.viewer_stats.total_command_stats.uniform_allocation_count),
        static_cast<double>(state.viewer_stats.total_command_stats.uniform_allocation_bytes) / 1024.0,
        static_cast<double>(state.editor_gpu_stats.command_stats.descriptor_ring_required_bytes) / 1024.0,
        static_cast<double>(state.editor_gpu_stats.command_stats.descriptor_ring_capacity_bytes) / 1024.0,
        static_cast<unsigned long long>(state.editor_gpu_stats.command_stats.descriptor_allocation_failure_count),
        static_cast<unsigned long long>(state.editor_gpu_stats.command_stats.resource_bind_failure_count),
        static_cast<unsigned long long>(state.editor_gpu_stats.command_stats.dropped_draw_count));

    std::string result = Rml::StringUtilities::EncodeRml(frame_text) + "<br/>" + Rml::StringUtilities::EncodeRml(cost_text) + "<br/>"
        + Rml::StringUtilities::EncodeRml(draw_text) + "<br/>" + Rml::StringUtilities::EncodeRml(view_text) + "<br/>" + Rml::StringUtilities::EncodeRml(pass_text)
        + "<br/>" + Rml::StringUtilities::EncodeRml(shadow_text);
    if (state.viewer_stats.model_count > 0 || state.viewer_stats.render_model_animation_sample_stats.input_count > 0
        || state.viewer_stats.prepared_model_surface_stats.draw_count > 0) {
        result += "<br/>";
        result += Rml::StringUtilities::EncodeRml(render_model_text);
    }
    if (state.viewer_stats.gpu_timer_count > 0) {
        result += "<br/>";
        result += Rml::StringUtilities::EncodeRml(gpu_text);
    }
    if (state.editor_gpu_stats.timer_count > 0) {
        result += "<br/>";
        result += Rml::StringUtilities::EncodeRml(editor_gpu_text);
    }
    if (state.viewer_stats.area_cache_record_count > 0) {
        result += "<br/>";
        result += Rml::StringUtilities::EncodeRml(area_cache_text);
        result += "<br/>";
        result += Rml::StringUtilities::EncodeRml(area_frame_text);
    }
    if (state.viewer_stats.forward_plus_cluster_count > 0) {
        result += "<br/>";
        result += Rml::StringUtilities::EncodeRml(forward_plus_text);
    }
    result += "<br/>";
    result += Rml::StringUtilities::EncodeRml(submit_text);
    return result;
}

void sync_viewer_fps_overlay(Rml::ElementDocument* fps_doc,
    ClientViewportRect rect,
    const ClientMetricsState& state,
    bool forward_plus_enabled, nw::render::ForwardPlusDebugMode debug_mode)
{
    auto* overlay = fps_doc ? fps_doc->GetElementById("viewer_fps_overlay") : nullptr;
    if (!overlay) {
        return;
    }

    if (!rect.valid()) {
        overlay->SetProperty("display", "none");
        return;
    }

    const bool verbose = viewer_fps_overlay_verbose();
    const int overlay_width = verbose ? 776 : 446;
    constexpr int kOverlayMargin = 8;
    const int rect_width = static_cast<int>(rect.width);
    const int left = std::max(rect.x + kOverlayMargin, rect.x + rect_width - overlay_width - kOverlayMargin);
    const int top = rect.y + kOverlayMargin;

    overlay->SetInnerRML(format_viewer_fps_rml(state, forward_plus_enabled, debug_mode));
    overlay->SetProperty("display", "block");
    overlay->SetProperty("width", std::to_string(verbose ? 760 : 430) + "px");
    overlay->SetProperty("height", std::to_string(verbose ? 144 : 54) + "px");
    overlay->SetProperty("left", std::to_string(left) + "px");
    overlay->SetProperty("top", std::to_string(top) + "px");
}

} // namespace nw::toolset
