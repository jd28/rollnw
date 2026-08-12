#include "ui_v1.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace nw::toolset {

namespace {

constexpr int kMaxQueuedEvents = 512;
constexpr size_t kMaxListRows = std::numeric_limits<uint16_t>::max();
constexpr int kMaxRowHeight = 4096;
constexpr int kMaxOverscan = 1024;
constexpr int kMaxColumns = 16;
constexpr size_t kMaxRefreshCallbacks = 64;

constexpr size_t event_slot(UiListEventType type)
{
    return static_cast<size_t>(type);
}

int logical_row_count(size_t item_count, int columns)
{
    return static_cast<int>((item_count + static_cast<size_t>(columns) - 1)
        / static_cast<size_t>(columns));
}

int logical_row_for_index(int index, int columns)
{
    return index < 0 ? -1 : index / columns;
}

} // namespace

void VirtualListHost::reset() noexcept
{
    lists_.clear();
    events_.clear();
    refresh_callbacks_.clear();
    generation_ = generation_ == std::numeric_limits<uint64_t>::max()
        ? 1
        : generation_ + 1;
}

bool VirtualListHost::create(std::string list_id, const UiListConfig& cfg)
{
    if (list_id.empty() || cfg.row_height <= 0 || cfg.row_height > kMaxRowHeight
        || cfg.overscan < 0 || cfg.overscan > kMaxOverscan
        || cfg.columns <= 0 || cfg.columns > kMaxColumns
        || lists_.contains(list_id)) {
        return false;
    }

    auto [it, inserted] = lists_.try_emplace(std::move(list_id));
    if (!inserted) {
        return false;
    }
    auto& state = it->second;
    state.config = cfg;
    state.controller.set_row_height(cfg.row_height);
    state.controller.set_overscan(cfg.overscan);
    return true;
}

bool VirtualListHost::destroy(std::string_view list_id)
{
    return lists_.erase(std::string(list_id)) > 0;
}

bool VirtualListHost::set_items(std::string_view list_id, std::vector<UiListItem> items)
{
    auto* state = get_state(list_id);
    if (!state || items.size() > kMaxListRows) {
        return false;
    }

    std::unordered_set<std::string, StringHash, StringEq> keys;
    keys.reserve(items.size());
    for (const auto& item : items) {
        if (item.key.empty() || item.cell_count == 0 || item.cell_count > item.cells.size()) {
            return false;
        }
        const uint8_t valid_mask = static_cast<uint8_t>((1u << item.cell_count) - 1u);
        if ((item.enabled_mask & static_cast<uint8_t>(~valid_mask)) != 0
            || !keys.emplace(item.key).second) {
            return false;
        }
    }

    std::string previous_key;
    const int previous_index = state->selected_index;
    if (previous_index >= 0 && previous_index < static_cast<int>(state->items.size())) {
        previous_key = state->items[static_cast<size_t>(previous_index)].key;
    }

    state->items = std::move(items);
    state->key_to_index.clear();
    state->key_to_index.reserve(state->items.size());
    for (size_t i = 0; i < state->items.size(); ++i) {
        state->key_to_index.emplace(state->items[i].key, static_cast<int>(i));
    }

    state->controller.set_total_rows(
        logical_row_count(state->items.size(), state->config.columns));

    int next_selected = -1;
    if (!previous_key.empty()) {
        auto it = state->key_to_index.find(previous_key);
        if (it != state->key_to_index.end()) {
            next_selected = it->second;
        }
    }
    state->selected_index = next_selected;
    state->controller.set_selected(
        logical_row_for_index(next_selected, state->config.columns));
    if (next_selected < 0
        || state->selected_cell >= state->items[static_cast<size_t>(next_selected)].cell_count) {
        state->selected_cell = -1;
    }
    if (state->hovered_index >= static_cast<int>(state->items.size())) {
        state->hovered_index = -1;
    }
    ++state->revision;
    return true;
}

bool VirtualListHost::set_selected(std::string_view list_id, const UiListSelection& selection, bool emit_event)
{
    auto* state = get_state(list_id);
    if (!state) {
        return false;
    }

    int resolved_index = -1;
    if (!selection.key.empty()) {
        auto it = state->key_to_index.find(selection.key);
        if (it == state->key_to_index.end()
            || (selection.index >= 0 && selection.index != it->second)) {
            return false;
        }
        resolved_index = it->second;
    } else if (selection.index >= 0 && selection.index < static_cast<int>(state->items.size())) {
        resolved_index = selection.index;
    } else if (selection.index >= 0 || selection.cell >= 0) {
        return false;
    }

    int resolved_cell = selection.cell;
    if (resolved_index >= 0 && resolved_cell >= 0) {
        const auto& item = state->items[static_cast<size_t>(resolved_index)];
        if (resolved_cell >= item.cell_count
            || (item.enabled_mask & (1u << resolved_cell)) == 0) {
            return false;
        }
    }

    state->selected_index = resolved_index;
    state->controller.set_selected(
        logical_row_for_index(resolved_index, state->config.columns));
    state->selected_cell = resolved_index >= 0 ? resolved_cell : -1;
    ++state->revision;
    if (emit_event && resolved_index >= 0 && !state->callbacks[event_slot(UiListEventType::select)].empty()) {
        UiListEvent event;
        event.type = UiListEventType::select;
        event.selection = selection_from(
            std::string(list_id), *state, resolved_index, state->selected_cell);
        enqueue_ordered(std::move(event));
    }
    return true;
}

std::optional<UiListSelection> VirtualListHost::get_selected(std::string_view list_id) const
{
    const auto* state = get_state(list_id);
    if (!state) {
        return std::nullopt;
    }

    return selection_from(
        std::string(list_id), *state, state->selected_index, state->selected_cell);
}

bool VirtualListHost::set_visible(std::string_view list_id, bool visible)
{
    auto* state = get_state(list_id);
    if (!state) {
        return false;
    }
    if (state->visible != visible) {
        state->visible = visible;
        ++state->revision;
    }
    return true;
}

bool VirtualListHost::set_title(std::string_view list_id, std::string title)
{
    auto* state = get_state(list_id);
    if (!state) {
        return false;
    }
    if (state->title != title) {
        state->title = std::move(title);
        ++state->revision;
    }
    return true;
}

std::optional<UiListWindow> VirtualListHost::window(
    std::string_view list_id, int viewport_height, int scroll_top)
{
    auto* state = get_state(list_id);
    if (!state) {
        return std::nullopt;
    }
    state->controller.set_viewport_height(viewport_height);
    state->controller.set_scroll_top(scroll_top);
    return UiListWindow{
        .items = state->items,
        .range = state->controller.compute_range(),
        .title = state->title,
        .revision = state->revision,
        .selected_index = state->selected_index,
        .selected_cell = state->selected_cell,
        .row_height = state->config.row_height,
        .columns = state->config.columns,
        .visible = state->visible,
    };
}

bool VirtualListHost::set_callback(std::string_view list_id, UiListEventType type, std::string function_name)
{
    auto* state = get_state(list_id);
    if (!state) {
        return false;
    }
    state->callbacks[event_slot(type)] = std::move(function_name);
    return true;
}

std::string VirtualListHost::callback(std::string_view list_id, UiListEventType type) const
{
    if (const auto* ptr = callback_ptr(list_id, type)) {
        return *ptr;
    }
    return {};
}

const std::string* VirtualListHost::callback_ptr(std::string_view list_id, UiListEventType type) const
{
    const auto* state = get_state(list_id);
    if (!state) {
        return nullptr;
    }
    const std::string& handler = state->callbacks[event_slot(type)];
    return handler.empty() ? nullptr : &handler;
}

bool VirtualListHost::push_hover(std::string_view list_id, int index)
{
    auto* state = get_state(list_id);
    if (!state) {
        return false;
    }
    state->hovered_index = (index >= 0 && index < static_cast<int>(state->items.size())) ? index : -1;

    if (!state->callbacks[event_slot(UiListEventType::hover)].empty()) {
        UiListEvent event;
        event.type = UiListEventType::hover;
        event.selection = selection_from(
            std::string(list_id), *state, state->hovered_index, -1);
        enqueue_coalesced(std::move(event));
    }
    return true;
}

bool VirtualListHost::push_activate(std::string_view list_id, int index, int cell)
{
    auto* state = get_state(list_id);
    if (!state || index < 0 || index >= static_cast<int>(state->items.size())) {
        return false;
    }
    const auto& item = state->items[static_cast<size_t>(index)];
    if (cell >= 0
        && (cell >= item.cell_count || (item.enabled_mask & (1u << cell)) == 0)) {
        return false;
    }

    state->selected_index = index;
    state->controller.set_selected(
        logical_row_for_index(index, state->config.columns));
    state->selected_cell = cell;
    ++state->revision;
    if (!state->callbacks[event_slot(UiListEventType::select)].empty()) {
        UiListEvent select_event;
        select_event.type = UiListEventType::select;
        select_event.selection = selection_from(std::string(list_id), *state, index, cell);
        enqueue_ordered(std::move(select_event));
    }

    if (!state->callbacks[event_slot(UiListEventType::activate)].empty()) {
        UiListEvent activate_event;
        activate_event.type = UiListEventType::activate;
        activate_event.selection = selection_from(std::string(list_id), *state, index, cell);
        enqueue_ordered(std::move(activate_event));
    }
    return true;
}

std::optional<int> VirtualListHost::move_and_activate(std::string_view list_id,
    int delta,
    int viewport_height,
    int scroll_top)
{
    auto* state = get_state(list_id);
    if (!state || state->items.empty() || delta == 0 || viewport_height <= 0) {
        return std::nullopt;
    }

    state->controller.set_viewport_height(viewport_height);
    state->controller.set_scroll_top(scroll_top);
    const int base = state->selected_index < 0
        ? (delta > 0 ? -1 : static_cast<int>(state->items.size()))
        : state->selected_index;
    const int selected = std::clamp(
        base + delta, 0, static_cast<int>(state->items.size()) - 1);
    if (selected < 0) {
        return std::nullopt;
    }
    const int logical_row = logical_row_for_index(selected, state->config.columns);
    state->controller.set_selected(logical_row);
    state->controller.set_scroll_top(
        state->controller.scroll_top_for_index(logical_row));
    if (!push_activate(list_id, selected)) {
        return std::nullopt;
    }
    return state->controller.scroll_top();
}

bool VirtualListHost::register_refresh_callback(std::string qualified_function)
{
    if (qualified_function.empty() || refresh_callbacks_.size() >= kMaxRefreshCallbacks
        || std::ranges::find(refresh_callbacks_, qualified_function) != refresh_callbacks_.end()) {
        return false;
    }
    refresh_callbacks_.push_back(std::move(qualified_function));
    return true;
}

std::span<const std::string> VirtualListHost::refresh_callbacks() const noexcept
{
    return refresh_callbacks_;
}

bool VirtualListHost::push_scroll(std::string_view list_id, int top, int start, int end)
{
    auto* state = get_state(list_id);
    if (!state) {
        return false;
    }
    if (!state->callbacks[event_slot(UiListEventType::scroll)].empty()) {
        UiListEvent event;
        event.type = UiListEventType::scroll;
        event.scroll = UiListScroll{std::string(list_id), top, start, end};
        enqueue_coalesced(std::move(event));
    }
    return true;
}

void VirtualListHost::drain_events(const std::function<void(const UiListEvent&)>& sink)
{
    if (!sink || events_.empty()) {
        return;
    }

    std::vector<UiListEvent> drained;
    drained.swap(events_);
    for (const auto& event : drained) {
        sink(event);
    }
}

VirtualListHost::ListState* VirtualListHost::get_state(std::string_view list_id)
{
    auto it = lists_.find(std::string(list_id));
    return it != lists_.end() ? &it->second : nullptr;
}

const VirtualListHost::ListState* VirtualListHost::get_state(std::string_view list_id) const
{
    auto it = lists_.find(std::string(list_id));
    return it != lists_.end() ? &it->second : nullptr;
}

UiListSelection VirtualListHost::selection_from(
    const std::string& list_id, const ListState& state, int index, int cell)
{
    UiListSelection out;
    out.list_id = list_id;
    out.index = index;
    out.cell = cell;
    if (index >= 0 && index < static_cast<int>(state.items.size())) {
        out.key = state.items[static_cast<size_t>(index)].key;
    }
    return out;
}

void VirtualListHost::enqueue_coalesced(UiListEvent event)
{
    for (auto& existing : events_) {
        if (existing.type == event.type
            && existing.selection.list_id == event.selection.list_id
            && existing.scroll.list_id == event.scroll.list_id
            && (event.type == UiListEventType::hover || event.type == UiListEventType::scroll)) {
            existing = std::move(event);
            return;
        }
    }

    enqueue_ordered(std::move(event));
}

void VirtualListHost::enqueue_ordered(UiListEvent event)
{
    if (static_cast<int>(events_.size()) >= kMaxQueuedEvents) {
        events_.erase(events_.begin());
    }
    events_.push_back(std::move(event));
}

VirtualListHost& ui_v1_host()
{
    static VirtualListHost host;
    return host;
}

} // namespace nw::toolset
