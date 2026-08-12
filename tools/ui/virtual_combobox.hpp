#pragma once

#include "virtual_list.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nw::toolset {

struct VirtualComboBoxItem {
    int32_t key = 0;
    std::string label;
    std::string detail;
};

struct VirtualComboBoxConfig {
    int row_height = 30;
    int visible_rows = 10;
    int overscan = 4;
};

struct VirtualComboBoxUpdate {
    std::string markup;
    int scroll_top = 0;
    bool replace_markup = false;
    bool set_scroll = false;
};

struct VirtualComboBoxRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct VirtualComboBoxPopupPlacement {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    bool above = false;

    bool operator==(const VirtualComboBoxPopupPlacement&) const = default;
};

// Aligns a popup to the anchor width, preferring space below and then above.
// Non-positive bounds, anchor width, or popup height produce an empty result.
[[nodiscard]] VirtualComboBoxPopupPlacement place_virtual_combobox_popup(
    VirtualComboBoxRect anchor,
    VirtualComboBoxRect bounds,
    int popup_height) noexcept;

// A single-active-combobox state machine. The caller owns the field and popup
// elements; this widget owns copied option rows, popup visibility, and virtual
// list state.
class VirtualComboBox {
public:
    // Non-positive geometry is clamped to one row/pixel; overscan is clamped
    // to zero and popup height saturates at the platform int limit.
    explicit VirtualComboBox(VirtualComboBoxConfig config = {});

    // Keys must be unique. Empty or duplicate-key batches are rejected and
    // leave the widget closed.
    bool open(std::vector<VirtualComboBoxItem> items, int32_t selected_key);
    void close() noexcept;

    [[nodiscard]] bool is_active() const noexcept;
    [[nodiscard]] bool popup_visible() const noexcept;
    [[nodiscard]] bool show_popup() noexcept;
    void hide_popup() noexcept;
    // The caller replaced its popup element; the next update must emit rows.
    void invalidate_popup_render() noexcept;
    // Selects an existing stable key. Missing keys are rejected without
    // changing the current selection.
    [[nodiscard]] bool select_key(int32_t key) noexcept;
    [[nodiscard]] int move_selection(int delta) noexcept;
    [[nodiscard]] std::optional<int32_t> selected_key() const noexcept;
    [[nodiscard]] int selected_index() const noexcept;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] int popup_height() const noexcept;

    // Aligns the popup to the anchor width, preferring the space below and
    // flipping above when that is the first placement fully inside bounds.
    // Non-positive bounds produce an empty placement.
    [[nodiscard]] VirtualComboBoxPopupPlacement place_popup(
        VirtualComboBoxRect anchor, VirtualComboBoxRect bounds) const noexcept;

    // Converts observed popup geometry into a bounded DOM update. Markup
    // contains only the visible range plus overscan and explicit spacers.
    [[nodiscard]] VirtualComboBoxUpdate update(
        int viewport_height, int observed_scroll_top, bool force);

private:
    VirtualComboBoxConfig config_;
    int popup_height_ = 0;
    std::vector<VirtualComboBoxItem> items_;
    VirtualListController list_;
    VirtualListRange rendered_range_{};
    int rendered_row_count_ = 0;
    bool rendered_ = false;
    bool scroll_to_selection_ = false;
    bool active_ = false;
    bool popup_visible_ = false;
};

} // namespace nw::toolset
