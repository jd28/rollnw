#pragma once

#include "ui_v1.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Rml {
class Element;
class ElementDocument;
}

namespace nw::toolset {

struct ManagedListRenderRecord {
    VirtualListRange range{};
    uint64_t revision = 0;
    int row_count = -1;
    // RmlUi clamps scroll before replacement markup is laid out. Retain one
    // requested offset for the following document update; -1 means none.
    int requested_scroll_top = -1;
    bool rendered = false;
};

struct ManagedListRenderState {
    std::unordered_map<std::string, ManagedListRenderRecord> lists;
    uint64_t host_generation = 0;
};

// Value-only request captured before list callbacks replace row markup. A
// non-negative cell selects that cell in the newly materialized selected row;
// -1 selects the declared element itself.
struct ManagedListFocusTarget {
    std::string element_id;
    int cell = -1;
};

// Renders the viewport-bounded rows in one immutable host window. Grid
// windows retain flat item indices while grouping markup by logical row.
[[nodiscard]] std::string render_managed_list_window(
    std::string_view list_id,
    const UiListWindow& window,
    std::string_view empty_text);

// Synchronizes every .managed_list_rows element in the document with the
// fixed-row list named by its data-list-id attribute. An element with
// data-scroll-selected="true" reveals a changed selection once, while later
// user scrolling remains unconstrained.
bool sync_managed_lists(Rml::ElementDocument* document,
    VirtualListHost& host,
    ManagedListRenderState& render_state,
    bool force);

// Positions visible .managed_list_popup elements from their flat RML
// attributes. The anchor is the selected row (or selected cell) in the named
// list, and the result is clamped to the named bounds element. Missing,
// invalid, or currently unmaterialized anchors leave the popup unchanged.
bool position_managed_list_popups(Rml::ElementDocument* document);

// True when the hit is inside a shared combobox field or popup. Null and
// unrelated elements are outside; callers use this boundary for dismissal.
[[nodiscard]] bool combobox_contains_element(Rml::Element* element);

// True only when the hit is inside a shared combobox popup. Wheel dispatch
// uses this narrower boundary so open options scroll instead of activating.
[[nodiscard]] bool combobox_popup_contains_element(Rml::Element* element);

// Captures an optional declarative focus target from the nearest managed list
// or cycling field. Missing or malformed attributes produce no request.
[[nodiscard]] std::optional<ManagedListFocusTarget> managed_list_focus_target(
    Rml::Element* element);

// Focuses the declared element or the requested cell in its selected row.
// Missing/unmaterialized targets fail closed without changing focus.
bool focus_managed_list_target(Rml::ElementDocument* document,
    const ManagedListFocusTarget& target);

// Resolves a managed row/cell hit and queues one activation in the host.
// Invalid list/index/cell data is rejected.
bool activate_managed_list_element(Rml::Element* element, VirtualListHost& host);

// Advances and activates the selected row for the nearest .managed_list_cycle
// element. data-cycle-list-id can redirect the operation to another host list;
// only a managed-list viewport receives the resulting scroll offset. Empty
// lists and zero deltas are rejected.
bool cycle_managed_list_element(
    Rml::Element* element, VirtualListHost& host, int delta);

} // namespace nw::toolset
