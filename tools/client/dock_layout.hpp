#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

enum class DockRegion {
    left,
    right,
    bottom,
};

[[nodiscard]] std::string_view dock_region_name(DockRegion region) noexcept;
[[nodiscard]] bool dock_region_from_string(std::string_view name, DockRegion& region) noexcept;

struct DockPaneState {
    DockRegion region = DockRegion::left;
    bool visible = false;
    int size_px = 0;
    int min_size_px = 0;
    int max_size_px = 0;
    std::string active_widget;
    std::vector<std::string> widgets;
};

class DockLayout {
public:
    DockLayout();

    [[nodiscard]] DockPaneState& pane(DockRegion region) noexcept;
    [[nodiscard]] const DockPaneState& pane(DockRegion region) const noexcept;

    [[nodiscard]] bool contains_widget(DockRegion region, std::string_view widget) const noexcept;
    [[nodiscard]] bool activate_widget(DockRegion region, std::string_view widget);
    void set_visible(DockRegion region, bool visible) noexcept;
    void set_size_px(DockRegion region, int size_px) noexcept;

private:
    DockPaneState left_;
    DockPaneState right_;
    DockPaneState bottom_;
};

} // namespace nw::toolset
