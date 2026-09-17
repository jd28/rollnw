#pragma once

#include "renderer.hpp"

#include <nw/render/forward_plus_debug_mode.hpp>
#include <nw/render/viewer/session.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Rml {
class ElementDocument;
class Context;
}

namespace nw::toolset {

// One application's HUD history is a true singleton. Inputs are borrowed only
// during update; owned renderer records retain the last received counters when
// snapshots are absent. Timing pairs retain valid latest/smoothed samples when
// negative samples arrive. No renderer borrow survives a frame or teardown.
Rml::ElementDocument* load_viewer_fps_document(Rml::Context& context);

struct ClientMetricsState {
    nw::render::viewer::ViewerFrameStats viewer_stats;
    ClientGpuFrameStats editor_gpu_stats;
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
