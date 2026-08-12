#include "virtual_list.hpp"

#include <algorithm>
#include <cassert>

namespace nw::toolset {

// ---- VirtualListController --------------------------------------------------

void VirtualListController::set_row_height(int px) noexcept
{
    assert(px > 0);
    row_height_px_ = std::max(1, px);
}

void VirtualListController::set_overscan(int rows) noexcept
{
    overscan_rows_ = std::max(0, rows);
}

void VirtualListController::set_total_rows(int count) noexcept
{
    total_rows_ = std::max(0, count);
    if (selected_index_ >= total_rows_) {
        selected_index_ = total_rows_ > 0 ? total_rows_ - 1 : -1;
    }
}

void VirtualListController::set_viewport_height(int px) noexcept
{
    viewport_height_px_ = std::max(0, px);
}

void VirtualListController::set_scroll_top(int px) noexcept
{
    const int max_scroll = std::max(0, total_rows_ * row_height_px_ - viewport_height_px_);
    scroll_top_px_ = std::clamp(px, 0, max_scroll);
}

void VirtualListController::set_selected(int index) noexcept
{
    if (total_rows_ == 0) {
        selected_index_ = -1;
    } else {
        selected_index_ = std::clamp(index, -1, total_rows_ - 1);
    }
}

int VirtualListController::move_selection(int delta) noexcept
{
    if (total_rows_ == 0) {
        selected_index_ = -1;
        return -1;
    }
    const int base = selected_index_ < 0 ? (delta > 0 ? -1 : total_rows_) : selected_index_;
    selected_index_ = std::clamp(base + delta, 0, total_rows_ - 1);
    return selected_index_;
}

int VirtualListController::scroll_top_for_index(int index) const noexcept
{
    if (index < 0 || total_rows_ == 0) {
        return scroll_top_px_;
    }
    const int clamped = std::clamp(index, 0, total_rows_ - 1);
    const int row_top = clamped * row_height_px_;
    const int row_bottom = row_top + row_height_px_;

    if (row_top < scroll_top_px_) {
        return row_top;
    }
    if (row_bottom > scroll_top_px_ + viewport_height_px_) {
        return row_bottom - viewport_height_px_;
    }
    return scroll_top_px_;
}

void VirtualListController::set_hovered(int index) noexcept
{
    hovered_index_ = (total_rows_ == 0) ? -1 : std::clamp(index, -1, total_rows_ - 1);
}

int VirtualListController::selected() const noexcept { return selected_index_; }
int VirtualListController::hovered() const noexcept { return hovered_index_; }
int VirtualListController::total_rows() const noexcept { return total_rows_; }
int VirtualListController::scroll_top() const noexcept { return scroll_top_px_; }
int VirtualListController::viewport_height() const noexcept { return viewport_height_px_; }

VirtualListRange VirtualListController::compute_range() const noexcept
{
    if (total_rows_ == 0 || viewport_height_px_ <= 0) {
        return {0, 0, 0, 0};
    }

    const int first_visible = scroll_top_px_ / row_height_px_;
    // ceil(viewport / row_height) to cover partially visible bottom rows
    const int visible_count = (viewport_height_px_ + row_height_px_ - 1) / row_height_px_;

    const int start = std::max(0, first_visible - overscan_rows_);
    const int end = std::min(total_rows_, first_visible + visible_count + overscan_rows_);

    return {
        start,
        end,
        start * row_height_px_,
        (total_rows_ - end) * row_height_px_,
    };
}

// ---- render_virtual_list ----------------------------------------------------

std::string render_virtual_list(const VirtualListController& ctrl,
    const VirtualListAdapter& adapter)
{
    if (adapter.size() == 0) {
        return {};
    }

    const VirtualListRange range = ctrl.compute_range();
    const std::string_view extra = adapter.row_extra_classes();
    const int selected = ctrl.selected();
    const int hovered = ctrl.hovered();

    std::string out;
    // Pre-allocation: spacers + conservative per-row budget for title/path markup.
    out.reserve(96 + static_cast<size_t>(range.end - range.start) * 256);

    // Top spacer
    if (range.top_spacer_px > 0) {
        out += "<div class=\"vl_spacer\" style=\"display:block;height:";
        out += std::to_string(range.top_spacer_px);
        out += "px\"></div>";
    }

    // Visible rows
    for (int i = range.start; i < range.end; ++i) {
        const bool is_selected = (i == selected);
        const bool is_hovered = (i == hovered);

        out += "<div class=\"vl_row";
        if (!extra.empty()) {
            out += ' ';
            out += extra;
        }
        if (is_selected) {
            out += " selected";
        }
        if (is_hovered) {
            out += " hovered";
        }
        out += "\" data-key=\"";
        out += std::to_string(adapter.row_key(i));
        out += "\">";
        out += adapter.render_row_inner(i, is_selected);
        out += "</div>";
    }

    // Bottom spacer
    if (range.bottom_spacer_px > 0) {
        out += "<div class=\"vl_spacer\" style=\"display:block;height:";
        out += std::to_string(range.bottom_spacer_px);
        out += "px\"></div>";
    }

    return out;
}

} // namespace nw::toolset
