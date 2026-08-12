#pragma once

#include "dock_layout.hpp"

#include <nw/render/render_context.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nw::toolset {

class ShellController {
public:
    bool showing_areas = false;
    bool showing_project_tree = false;
    bool command_palette_visible = false;
    bool viewer_area_lights_enabled = true;
    bool viewer_area_debug_enabled = true;
    bool viewer_area_triggers_enabled = true;
    bool viewer_area_encounters_enabled = true;
    bool viewer_forward_plus_enabled = true;
    bool viewer_forward_plus_auto_configure_area = true;
    uint32_t viewer_forward_plus_tile_size = 64;
    uint32_t viewer_forward_plus_depth_slices = 8;
    uint32_t viewer_forward_plus_max_lights_per_cluster = 128;
    nw::render::ForwardPlusDebugMode viewer_forward_plus_debug_mode = nw::render::ForwardPlusDebugMode::off;
    bool viewer_area_fog_enabled = false;
    bool viewer_area_shadows_enabled = true;
    bool viewer_area_day_night_autoplay = true;
    float viewer_area_day_night_elapsed_seconds = 0.0f;
    uint64_t viewer_area_day_night_time_generation = 0;
    uint64_t viewer_area_reload_generation = 0;
    DockLayout docks;

    std::vector<std::string> output_channels = {"info", "warn", "error", "script"};

    bool output_dirty = true;
    bool terminal_dirty = true;

    std::vector<std::pair<std::string, std::string>> output_lines;
    std::vector<std::pair<std::string, std::string>> terminal_lines;

    void set_showing_areas(bool visible) noexcept;
    void set_showing_project_tree(bool visible) noexcept;
    void set_command_palette_visible(bool visible) noexcept;
    void set_terminal_visible(bool visible);
    void set_output_panel_visible(bool visible);
    [[nodiscard]] bool terminal_visible() const noexcept;
    [[nodiscard]] bool output_panel_visible() const noexcept;
    [[nodiscard]] bool bottom_dock_visible() const noexcept;
    void set_bottom_dock_visible(bool visible) noexcept;
    void set_bottom_dock_size_px(int size_px) noexcept;
    [[nodiscard]] bool activate_bottom_dock_widget(std::string_view widget);
    [[nodiscard]] bool output_channel_visible(std::string_view channel) const noexcept;
    bool toggle_output_channel(std::string_view channel);
    void observe_output_scroll(float scroll_top, float scroll_height, float client_height) noexcept;
    [[nodiscard]] bool output_follows_tail() const noexcept;

    void append_output(std::string_view channel, std::string_view line);
    void append_terminal(std::string_view style, std::string_view line);
    void clear_terminal() noexcept;

private:
    bool output_follows_tail_ = true;
};

} // namespace nw::toolset
