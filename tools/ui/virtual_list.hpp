#pragma once

#include <string>
#include <string_view>

namespace nw::toolset {

struct VirtualListRange {
    int start; // inclusive
    int end;   // exclusive
    int top_spacer_px;
    int bottom_spacer_px;
};

class VirtualListController {
public:
    VirtualListController() = default;

    void set_row_height(int px) noexcept; // default: 30
    void set_overscan(int rows) noexcept; // default: 6
    void set_total_rows(int count) noexcept;
    void set_viewport_height(int px) noexcept;
    void set_scroll_top(int px) noexcept;
    void set_selected(int index) noexcept; // -1 = none
    void set_hovered(int index) noexcept;  // -1 = none

    // Clamps delta to [0, total_rows). Returns new index, or -1 if empty.
    int move_selection(int delta) noexcept;

    // Smallest scroll_top that makes index fully visible; current value if already visible.
    [[nodiscard]] int scroll_top_for_index(int index) const noexcept;

    [[nodiscard]] int selected() const noexcept;
    [[nodiscard]] int hovered() const noexcept;
    [[nodiscard]] int total_rows() const noexcept;
    [[nodiscard]] int scroll_top() const noexcept;
    [[nodiscard]] int viewport_height() const noexcept;
    [[nodiscard]] VirtualListRange compute_range() const noexcept;

private:
    int row_height_px_ = 30;
    int overscan_rows_ = 6;
    int total_rows_ = 0;
    int viewport_height_px_ = 0;
    int scroll_top_px_ = 0;
    int selected_index_ = -1;
    int hovered_index_ = -1;
};

class VirtualListAdapter {
public:
    virtual ~VirtualListAdapter() = default;
    [[nodiscard]] virtual int size() const = 0;
    [[nodiscard]] virtual int row_key(int i) const { return i; }
    // Space-separated CSS classes added to each row wrapper alongside "vl_row"
    [[nodiscard]] virtual std::string_view row_extra_classes() const { return {}; }
    // Inner HTML for one row; the wrapper div is produced by render_virtual_list
    [[nodiscard]] virtual std::string render_row_inner(int i, bool selected) const = 0;
};

// Produces innerHTML for the scroll container:
//
//   <div class="vl_spacer" style="display:block;height:Xpx"></div> ← top spacer
//   <div class="vl_row [extras] [selected]" data-key="N">…</div>  ← visible rows only
//   <div class="vl_spacer" style="display:block;height:Xpx"></div> ← bottom spacer
//
// data-key uses adapter.row_key(i), defaulting to absolute row index.
// Returns an empty string if the adapter reports zero rows.
[[nodiscard]] std::string render_virtual_list(const VirtualListController& ctrl,
    const VirtualListAdapter& adapter);

} // namespace nw::toolset
