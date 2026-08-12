#pragma once

#include <nw/render/render_context.hpp>

#include <cstdint>

inline constexpr const char* kClientGpuTimerUi = "rollnw.client.ui";
inline constexpr const char* kClientGpuTimerViewport = "rollnw.client.viewport";
inline constexpr const char* kClientGpuTimerOverlay = "rollnw.client.overlay";
inline constexpr const char* kClientGpuTimerPalette = "rollnw.client.palette";

struct ClientViewportRect {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return width > 0 && height > 0;
    }
};

struct ClientGpuTimerScope {
    uint32_t index = UINT32_MAX;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != UINT32_MAX; }
};

struct ClientGpuFrameStats {
    float ui_seconds = 0.0f;
    float viewport_seconds = 0.0f;
    float overlay_seconds = 0.0f;
    float palette_seconds = 0.0f;
    float total_seconds = 0.0f;
    uint32_t timer_count = 0;
    nw::gfx::CommandStats command_stats;
};

struct ClientAreaViewerOptions {
    bool lights_enabled = true;
    bool debug_enabled = true;
    bool triggers_enabled = true;
    bool encounters_enabled = true;
    bool forward_plus_enabled = true;
    bool forward_plus_auto_configure_area = true;
    uint32_t forward_plus_tile_size = 64;
    uint32_t forward_plus_depth_slices = 8;
    uint32_t forward_plus_max_lights_per_cluster = 128;
    nw::render::ForwardPlusDebugMode forward_plus_debug_mode = nw::render::ForwardPlusDebugMode::off;
    bool fog_enabled = false;
    bool shadows_enabled = true;
    bool day_night_autoplay = true;
    float day_night_elapsed_seconds = 0.0f;
    uint64_t day_night_time_generation = 0;
    uint64_t reload_generation = 0;
};

enum class ClientViewportDragMode {
    look,
    pan,
};

enum class ClientAreaSelectionTarget {
    object,
    tile,
};

enum class ClientViewportCameraCommand {
    move_forward,
    move_backward,
    move_left,
    move_right,
    move_up,
    move_down,
    yaw_left,
    yaw_right,
    pitch_up,
    pitch_down,
    zoom_in,
    zoom_out,
    fit,
    gameplay,
};
