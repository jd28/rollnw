#include "dock_layout.hpp"

#include <algorithm>

namespace nw::toolset {

namespace {

DockPaneState make_left_dock()
{
    DockPaneState pane;
    pane.region = DockRegion::left;
    pane.visible = false;
    pane.size_px = 360;
    pane.min_size_px = 260;
    pane.max_size_px = 640;
    pane.active_widget = "project_navigator";
    pane.widgets = {"project_navigator", "area_navigator"};
    return pane;
}

DockPaneState make_right_dock()
{
    DockPaneState pane;
    pane.region = DockRegion::right;
    pane.visible = false;
    pane.size_px = 320;
    pane.min_size_px = 240;
    pane.max_size_px = 640;
    pane.active_widget = "inspector";
    pane.widgets = {"inspector"};
    return pane;
}

DockPaneState make_bottom_dock()
{
    DockPaneState pane;
    pane.region = DockRegion::bottom;
    pane.visible = false;
    pane.size_px = 240;
    pane.min_size_px = 160;
    pane.max_size_px = 720;
    pane.active_widget = "output";
    pane.widgets = {"output", "terminal"};
    return pane;
}

bool has_widget(const DockPaneState& pane, std::string_view widget) noexcept
{
    return std::find_if(pane.widgets.begin(), pane.widgets.end(), [widget](const std::string& candidate) {
        return candidate == widget;
    }) != pane.widgets.end();
}

} // namespace

std::string_view dock_region_name(DockRegion region) noexcept
{
    switch (region) {
    case DockRegion::left:
        return "left";
    case DockRegion::right:
        return "right";
    case DockRegion::bottom:
        return "bottom";
    }
    return {};
}

bool dock_region_from_string(std::string_view name, DockRegion& region) noexcept
{
    if (name == "left") {
        region = DockRegion::left;
        return true;
    }
    if (name == "right") {
        region = DockRegion::right;
        return true;
    }
    if (name == "bottom") {
        region = DockRegion::bottom;
        return true;
    }
    return false;
}

DockLayout::DockLayout()
    : left_(make_left_dock())
    , right_(make_right_dock())
    , bottom_(make_bottom_dock())
{
}

DockPaneState& DockLayout::pane(DockRegion region) noexcept
{
    switch (region) {
    case DockRegion::left:
        return left_;
    case DockRegion::right:
        return right_;
    case DockRegion::bottom:
        return bottom_;
    }
    return left_;
}

const DockPaneState& DockLayout::pane(DockRegion region) const noexcept
{
    switch (region) {
    case DockRegion::left:
        return left_;
    case DockRegion::right:
        return right_;
    case DockRegion::bottom:
        return bottom_;
    }
    return left_;
}

bool DockLayout::contains_widget(DockRegion region, std::string_view widget) const noexcept
{
    return has_widget(pane(region), widget);
}

bool DockLayout::activate_widget(DockRegion region, std::string_view widget)
{
    auto& target = pane(region);
    if (!has_widget(target, widget)) {
        return false;
    }

    target.active_widget = widget;
    target.visible = true;
    return true;
}

void DockLayout::set_visible(DockRegion region, bool visible) noexcept
{
    pane(region).visible = visible;
}

void DockLayout::set_size_px(DockRegion region, int size_px) noexcept
{
    pane(region).size_px = size_px;
}

} // namespace nw::toolset
