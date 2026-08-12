#pragma once

#include "ui_v1.hpp"

#include <cstdint>
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
    bool rendered = false;
};

struct ManagedListRenderState {
    std::unordered_map<std::string, ManagedListRenderRecord> lists;
    uint64_t host_generation = 0;
};

// Renders the viewport-bounded rows in one immutable host window. Grid
// windows retain flat item indices while grouping markup by logical row.
[[nodiscard]] std::string render_managed_list_window(
    std::string_view list_id,
    const UiListWindow& window,
    std::string_view empty_text);

// Synchronizes every .managed_list_rows element in the document with the
// fixed-row list named by its data-list-id attribute.
bool sync_managed_lists(Rml::ElementDocument* document,
    VirtualListHost& host,
    ManagedListRenderState& render_state,
    bool force);

// Resolves a managed row/cell hit and queues one activation in the host.
// Returns false when the element is not part of a managed list or carries
// invalid list/index/cell data.
bool activate_managed_list_element(Rml::Element* element, VirtualListHost& host);

// Advances and activates the selected row for the nearest
// .managed_list_cycle element. Empty lists and zero deltas are rejected.
bool cycle_managed_list_element(
    Rml::Element* element, VirtualListHost& host, int delta);

} // namespace nw::toolset
