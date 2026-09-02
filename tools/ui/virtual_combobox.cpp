#include "virtual_combobox.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace nw::toolset {
namespace {

std::string escape_markup(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&#39;";
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}

class ComboBoxListAdapter final : public VirtualListAdapter {
public:
    explicit ComboBoxListAdapter(const std::vector<VirtualComboBoxItem>& items)
        : items_{items}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(items_.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return items_[static_cast<size_t>(index)].key;
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "combobox_option";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        const auto& item = items_[static_cast<size_t>(index)];
        std::string markup;
        markup.reserve(item.label.size() + item.detail.size() + 128);
        markup += "<span class=\"combobox_option_value\">";
        markup += escape_markup(item.label);
        markup += "</span>";
        if (!item.detail.empty()) {
            markup += "<span class=\"combobox_option_detail\">";
            markup += escape_markup(item.detail);
            markup += "</span>";
        }
        return markup;
    }

private:
    const std::vector<VirtualComboBoxItem>& items_;
};

} // namespace

VirtualComboBox::VirtualComboBox(VirtualComboBoxConfig config)
    : config_{config}
{
    config_.row_height = std::max(1, config.row_height);
    config_.visible_rows = std::max(1, config.visible_rows);
    config_.overscan = std::max(0, config.overscan);
    const int64_t popup_height = static_cast<int64_t>(config_.visible_rows)
        * static_cast<int64_t>(config_.row_height);
    popup_height_ = static_cast<int>(std::min<int64_t>(
        popup_height, std::numeric_limits<int>::max()));
    list_.set_row_height(config_.row_height);
    list_.set_overscan(config_.overscan);
    list_.set_viewport_height(popup_height_);
}

bool VirtualComboBox::open(
    std::vector<VirtualComboBoxItem> items, int32_t selected_key)
{
    if (items.empty()
        || items.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        close();
        return false;
    }

    std::unordered_set<int32_t> keys;
    keys.reserve(items.size());
    for (const auto& item : items) {
        if (!keys.insert(item.key).second) {
            close();
            return false;
        }
    }
    int selected = -1;
    for (size_t index = 0; index < items.size(); ++index) {
        if (items[index].key == selected_key) {
            selected = static_cast<int>(index);
            break;
        }
    }

    items_ = std::move(items);
    list_.set_total_rows(static_cast<int>(items_.size()));
    list_.set_viewport_height(popup_height_);
    list_.set_scroll_top(0);
    list_.set_selected(selected);
    rendered_ = false;
    scroll_to_selection_ = true;
    active_ = true;
    popup_visible_ = true;
    return true;
}

void VirtualComboBox::close() noexcept
{
    items_.clear();
    list_.set_total_rows(0);
    list_.set_scroll_top(0);
    list_.set_selected(-1);
    rendered_ = false;
    scroll_to_selection_ = false;
    active_ = false;
    popup_visible_ = false;
}

bool VirtualComboBox::is_active() const noexcept
{
    return active_;
}

bool VirtualComboBox::popup_visible() const noexcept
{
    return popup_visible_;
}

bool VirtualComboBox::show_popup() noexcept
{
    if (!active_) {
        return false;
    }
    popup_visible_ = true;
    rendered_ = false;
    scroll_to_selection_ = true;
    return true;
}

void VirtualComboBox::hide_popup() noexcept
{
    popup_visible_ = false;
}

void VirtualComboBox::invalidate_popup_render() noexcept
{
    rendered_ = false;
}

bool VirtualComboBox::select_key(int32_t key) noexcept
{
    if (!active_) {
        return false;
    }

    for (size_t index = 0; index < items_.size(); ++index) {
        if (items_[index].key != key) {
            continue;
        }

        const int selected = static_cast<int>(index);
        if (list_.selected() != selected) {
            list_.set_selected(selected);
            scroll_to_selection_ = true;
            rendered_ = false;
        }
        return true;
    }
    return false;
}

int VirtualComboBox::move_selection(int delta) noexcept
{
    const int selected = list_.move_selection(delta);
    scroll_to_selection_ = selected >= 0;
    rendered_ = false;
    return selected;
}

std::optional<int32_t> VirtualComboBox::selected_key() const noexcept
{
    const int selected = list_.selected();
    if (!active_ || selected < 0 || selected >= static_cast<int>(items_.size())) {
        return std::nullopt;
    }
    return items_[static_cast<size_t>(selected)].key;
}

int VirtualComboBox::selected_index() const noexcept
{
    return list_.selected();
}

size_t VirtualComboBox::size() const noexcept
{
    return items_.size();
}

int VirtualComboBox::popup_height() const noexcept
{
    return popup_height_;
}

VirtualComboBoxPopupPlacement place_virtual_combobox_popup(
    VirtualComboBoxRect anchor,
    VirtualComboBoxRect bounds,
    int popup_height) noexcept
{
    VirtualComboBoxPopupPlacement result;
    if (bounds.width <= 0 || bounds.height <= 0 || anchor.width <= 0
        || popup_height <= 0) {
        return result;
    }

    const int64_t bounds_left = bounds.x;
    const int64_t bounds_top = bounds.y;
    const int64_t bounds_right = bounds_left + bounds.width;
    const int64_t bounds_bottom = bounds_top + bounds.height;
    result.width = std::min(anchor.width, bounds.width);
    result.height = std::min(popup_height, bounds.height);

    const int64_t max_left = bounds_right - result.width;
    const int64_t max_top = bounds_bottom - result.height;
    result.left = static_cast<int>(std::clamp<int64_t>(
        anchor.x, bounds_left, max_left));

    const int64_t below = static_cast<int64_t>(anchor.y)
        + std::max(0, anchor.height);
    const int64_t above = static_cast<int64_t>(anchor.y) - result.height;
    if (below + result.height <= bounds_bottom) {
        result.top = static_cast<int>(std::max(below, bounds_top));
    } else if (above >= bounds_top) {
        result.top = static_cast<int>(above);
        result.above = true;
    } else {
        result.top = static_cast<int>(std::clamp<int64_t>(
            below, bounds_top, max_top));
    }
    return result;
}

VirtualComboBoxPopupPlacement VirtualComboBox::place_popup(
    VirtualComboBoxRect anchor, VirtualComboBoxRect bounds) const noexcept
{
    return place_virtual_combobox_popup(anchor, bounds, popup_height_);
}

VirtualComboBoxUpdate VirtualComboBox::update(
    int viewport_height, int observed_scroll_top, bool force)
{
    VirtualComboBoxUpdate result;
    if (!active_ || !popup_visible_) {
        return result;
    }

    list_.set_viewport_height(std::max(0, viewport_height));
    list_.set_scroll_top(std::max(0, observed_scroll_top));
    result.scroll_top = list_.scroll_top();
    if (scroll_to_selection_) {
        result.scroll_top = list_.scroll_top_for_index(list_.selected());
        list_.set_scroll_top(result.scroll_top);
        result.set_scroll = result.scroll_top != observed_scroll_top;
        scroll_to_selection_ = result.set_scroll;
    }

    const auto range = list_.compute_range();
    const int row_count = static_cast<int>(items_.size());
    if (!force && rendered_ && row_count == rendered_row_count_
        && range.start == rendered_range_.start && range.end == rendered_range_.end) {
        return result;
    }

    result.markup = render_virtual_list(list_, ComboBoxListAdapter{items_});
    result.replace_markup = true;
    rendered_range_ = range;
    rendered_row_count_ = row_count;
    rendered_ = true;
    return result;
}

} // namespace nw::toolset
