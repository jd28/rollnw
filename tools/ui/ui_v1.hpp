#pragma once

#include "virtual_list.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace nw::toolset {

struct UiListConfig {
    int row_height = 30;
    int overscan = 6;
    int columns = 1;
};

struct UiListItem {
    std::string key;
    std::string icon_source;
    std::array<std::string, 4> cells;
    uint8_t cell_count = 1;
    uint8_t enabled_mask = 1;
};

struct UiListSelection {
    std::string list_id;
    std::string key;
    int index = -1;
    int cell = -1;
};

struct UiListScroll {
    std::string list_id;
    int top = 0;
    int start = 0;
    int end = 0;
};

enum class UiListEventType : uint8_t {
    hover,
    select,
    activate,
    scroll,
};

struct UiListEvent {
    UiListEventType type = UiListEventType::hover;
    UiListSelection selection;
    UiListScroll scroll;
};

// The host owns every string and row. This view is valid until the same host
// mutates or destroys the named list and is consumed synchronously on the one
// client UI thread.
struct UiListWindow {
    std::span<const UiListItem> items;
    VirtualListRange range{};
    std::string_view title;
    uint64_t revision = 0;
    int selected_index = -1;
    int selected_cell = -1;
    int row_height = 30;
    int columns = 1;
    bool visible = true;
};

struct StringHash {
    using is_transparent = void;

    size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
    size_t operator()(const std::string& value) const noexcept { return std::hash<std::string_view>{}(value); }
};

struct StringEq {
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept { return lhs == rhs; }
    bool operator()(const std::string& lhs, std::string_view rhs) const noexcept { return lhs == rhs; }
    bool operator()(std::string_view lhs, const std::string& rhs) const noexcept { return lhs == rhs; }
    bool operator()(const std::string& lhs, const std::string& rhs) const noexcept { return lhs == rhs; }
};

class VirtualListHost {
public:
    void reset() noexcept;
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool contains(std::string_view list_id) const;

    bool create(std::string list_id, const UiListConfig& cfg);
    bool destroy(std::string_view list_id);

    bool set_items(std::string_view list_id, std::vector<UiListItem> items);
    bool set_selected(std::string_view list_id, const UiListSelection& selection, bool emit_event = true);
    std::optional<UiListSelection> get_selected(std::string_view list_id) const;
    bool set_visible(std::string_view list_id, bool visible);
    bool set_title(std::string_view list_id, std::string title);
    std::optional<UiListWindow> window(
        std::string_view list_id, int viewport_height, int scroll_top);

    bool set_callback(std::string_view list_id, UiListEventType type, std::string function_name);
    std::string callback(std::string_view list_id, UiListEventType type) const;
    const std::string* callback_ptr(std::string_view list_id, UiListEventType type) const;

    bool push_hover(std::string_view list_id, int index);
    bool push_activate(std::string_view list_id, int index, int cell = -1);
    bool push_scroll(std::string_view list_id, int top, int start, int end);
    std::optional<int> move_and_activate(std::string_view list_id,
        int delta,
        int viewport_height,
        int scroll_top);

    bool register_refresh_callback(std::string qualified_function);
    [[nodiscard]] std::span<const std::string> refresh_callbacks() const noexcept;

    void drain_events(const std::function<void(const UiListEvent&)>& sink);

private:
    struct ListState {
        UiListConfig config;
        std::vector<UiListItem> items;
        std::unordered_map<std::string, int, StringHash, StringEq> key_to_index;
        VirtualListController controller;
        int hovered_index = -1;
        int selected_index = -1;
        int selected_cell = -1;
        std::array<std::string, 4> callbacks;
        std::string title;
        uint64_t revision = 1;
        bool visible = true;
    };

    ListState* get_state(std::string_view list_id);
    const ListState* get_state(std::string_view list_id) const;
    static UiListSelection selection_from(
        const std::string& list_id, const ListState& state, int index, int cell);

    void enqueue_coalesced(UiListEvent event);
    void enqueue_ordered(UiListEvent event);

    std::unordered_map<std::string, ListState, StringHash, StringEq> lists_;
    std::vector<UiListEvent> events_;
    std::vector<std::string> refresh_callbacks_;
    uint64_t generation_ = 1;
};

VirtualListHost& ui_v1_host();

} // namespace nw::toolset
