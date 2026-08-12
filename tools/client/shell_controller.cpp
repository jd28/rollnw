#include "shell_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace nw::toolset {

namespace {

constexpr size_t kMaxShellLines = 400;
constexpr size_t kPruneShellLines = 100;
constexpr float kOutputBottomTolerancePx = 1.0f;
constexpr std::string_view kOutputWidget = "output";
constexpr std::string_view kTerminalWidget = "terminal";

bool is_output_channel(std::string_view channel) noexcept
{
    return channel == "info" || channel == "warn" || channel == "error" || channel == "script";
}

void append_bounded(std::vector<std::pair<std::string, std::string>>& lines,
    std::string_view key,
    std::string_view line)
{
    lines.emplace_back(std::string(key), std::string(line));
    if (lines.size() > kMaxShellLines) {
        lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(kPruneShellLines));
    }
}

} // namespace

void ShellController::set_showing_areas(bool visible) noexcept
{
    showing_areas = visible;
    if (visible) {
        showing_project_tree = false;
        if (!docks.activate_widget(DockRegion::left, "area_navigator")) {
            showing_areas = false;
            docks.set_visible(DockRegion::left, false);
        }
    } else if (!showing_project_tree) {
        docks.set_visible(DockRegion::left, false);
    }
}

void ShellController::set_showing_project_tree(bool visible) noexcept
{
    showing_project_tree = visible;
    if (visible) {
        showing_areas = false;
        if (!docks.activate_widget(DockRegion::left, "project_navigator")) {
            showing_project_tree = false;
            docks.set_visible(DockRegion::left, false);
        }
    } else if (!showing_areas) {
        docks.set_visible(DockRegion::left, false);
    }
}

void ShellController::set_command_palette_visible(bool visible) noexcept
{
    command_palette_visible = visible;
}

void ShellController::set_terminal_visible(bool visible)
{
    if (visible) {
        if (!docks.activate_widget(DockRegion::bottom, kTerminalWidget)) {
            return;
        }
    } else if (terminal_visible()) {
        docks.set_visible(DockRegion::bottom, false);
    }
    terminal_dirty = true;
}

void ShellController::set_output_panel_visible(bool visible)
{
    if (visible) {
        if (!docks.activate_widget(DockRegion::bottom, kOutputWidget)) {
            return;
        }
    } else if (output_panel_visible()) {
        docks.set_visible(DockRegion::bottom, false);
    }
    output_dirty = true;
}

bool ShellController::terminal_visible() const noexcept
{
    const auto& bottom = docks.pane(DockRegion::bottom);
    return bottom.visible && bottom.active_widget == kTerminalWidget;
}

bool ShellController::output_panel_visible() const noexcept
{
    const auto& bottom = docks.pane(DockRegion::bottom);
    return bottom.visible && bottom.active_widget == kOutputWidget;
}

bool ShellController::bottom_dock_visible() const noexcept
{
    return docks.pane(DockRegion::bottom).visible;
}

void ShellController::set_bottom_dock_visible(bool visible) noexcept
{
    docks.set_visible(DockRegion::bottom, visible);
    output_dirty = true;
    terminal_dirty = true;
}

void ShellController::set_bottom_dock_size_px(int size_px) noexcept
{
    docks.set_size_px(DockRegion::bottom, size_px);
}

bool ShellController::activate_bottom_dock_widget(std::string_view widget)
{
    if (!docks.activate_widget(DockRegion::bottom, widget)) {
        return false;
    }

    if (widget == kOutputWidget) {
        output_dirty = true;
    } else if (widget == kTerminalWidget) {
        terminal_dirty = true;
    }
    return true;
}

bool ShellController::output_channel_visible(std::string_view channel) const noexcept
{
    const auto it = std::find_if(output_channels.begin(), output_channels.end(),
        [channel](const std::string& active) {
            return std::string_view(active) == channel;
        });
    return it != output_channels.end();
}

bool ShellController::toggle_output_channel(std::string_view channel)
{
    if (!is_output_channel(channel)) {
        return false;
    }

    const auto it = std::find_if(output_channels.begin(), output_channels.end(),
        [channel](const std::string& active) {
            return std::string_view(active) == channel;
        });
    if (it == output_channels.end()) {
        output_channels.emplace_back(channel);
    } else {
        output_channels.erase(it);
    }
    output_dirty = true;
    return true;
}

void ShellController::observe_output_scroll(float scroll_top, float scroll_height, float client_height) noexcept
{
    if (!std::isfinite(scroll_top) || !std::isfinite(scroll_height)
        || !std::isfinite(client_height) || scroll_top < 0.0f
        || scroll_height < 0.0f || client_height < 0.0f) {
        output_follows_tail_ = false;
        return;
    }

    const float max_scroll_top = std::max(0.0f, scroll_height - client_height);
    const float clamped_scroll_top = std::clamp(scroll_top, 0.0f, max_scroll_top);
    output_follows_tail_ = max_scroll_top - clamped_scroll_top <= kOutputBottomTolerancePx;
}

bool ShellController::output_follows_tail() const noexcept
{
    return output_follows_tail_;
}

void ShellController::append_output(std::string_view channel, std::string_view line)
{
    append_bounded(output_lines, channel, line);
    output_dirty = true;
}

void ShellController::append_terminal(std::string_view style, std::string_view line)
{
    append_bounded(terminal_lines, style, line);
    terminal_dirty = true;
}

void ShellController::clear_terminal() noexcept
{
    terminal_lines.clear();
    terminal_dirty = true;
}

} // namespace nw::toolset
