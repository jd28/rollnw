#include "rml_managed_list.hpp"

#include "virtual_combobox.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace nw::toolset {
namespace {

std::string escape_markup(std::string_view input)
{
    std::string output;
    output.reserve(input.size());
    for (const char ch : input) {
        switch (ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        case '\'':
            output += "&#39;";
            break;
        default:
            output += ch;
            break;
        }
    }
    return output;
}

std::optional<int> parse_int(std::string_view value)
{
    int result = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

Rml::Element* ancestor_with_class(Rml::Element* element, std::string_view class_name)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->IsClassSet(class_name.data())) {
            return cursor;
        }
    }
    return nullptr;
}

void append_spacer(std::string& markup, int height)
{
    if (height <= 0) {
        return;
    }
    markup += "<div class=\"managed_list_spacer\" style=\"height:";
    markup += std::to_string(height);
    markup += "px\"></div>";
}

std::string column_width(int columns)
{
    const int hundredths = 10'000 / columns;
    std::string result = std::to_string(hundredths / 100);
    const int fraction = hundredths % 100;
    if (fraction != 0) {
        result += '.';
        if (fraction < 10) {
            result += '0';
        }
        result += std::to_string(fraction);
    }
    return result;
}

void append_item(std::string& markup,
    std::string_view list_id,
    const UiListWindow& window,
    int index,
    std::string_view extra_class,
    std::string_view style)
{
    const auto& item = window.items[static_cast<size_t>(index)];
    markup += "<div class=\"managed_list_row";
    if (!extra_class.empty()) {
        markup += ' ';
        markup += extra_class;
    }
    if (index == window.selected_index) {
        markup += " selected";
    }
    markup += "\"";
    if (!style.empty()) {
        markup += " style=\"";
        markup += style;
        markup += "\"";
    }
    markup += " data-list-id=\"";
    markup += escape_markup(list_id);
    markup += "\" data-index=\"";
    markup += std::to_string(index);
    markup += "\" data-key=\"";
    markup += escape_markup(item.key);
    markup += "\">";
    if (!item.icon_source.empty()) {
        markup += "<img class=\"managed_list_icon\" src=\"";
        markup += escape_markup(item.icon_source);
        markup += "\" />";
    }
    for (int cell = 0; cell < item.cell_count; ++cell) {
        const bool enabled = (item.enabled_mask & (1u << cell)) != 0;
        markup += "<span class=\"managed_list_cell cell_";
        markup += std::to_string(cell);
        if (!enabled) {
            markup += " disabled";
        }
        if (index == window.selected_index && cell == window.selected_cell) {
            markup += " selected";
        }
        markup += "\" data-list-id=\"";
        markup += escape_markup(list_id);
        markup += "\" data-index=\"";
        markup += std::to_string(index);
        markup += "\" data-cell=\"";
        markup += std::to_string(cell);
        markup += "\">";
        markup += escape_markup(item.cells[static_cast<size_t>(cell)]);
        markup += "</span>";
    }
    markup += "</div>";
}

std::string render_window(std::string_view list_id,
    const UiListWindow& window,
    std::string_view empty_text)
{
    if (window.items.empty()) {
        std::string markup = "<div class=\"managed_list_empty\">";
        markup += escape_markup(empty_text);
        markup += "</div>";
        return markup;
    }

    std::string markup;
    const int visible_rows = window.range.end - window.range.start;
    const int visible_items = std::max(0, visible_rows) * window.columns;
    markup.reserve(128 + static_cast<size_t>(visible_items) * 320);
    append_spacer(markup, window.range.top_spacer_px);
    if (window.columns == 1) {
        for (int index = window.range.start; index < window.range.end; ++index) {
            append_item(markup, list_id, window, index, {}, {});
        }
    } else {
        const std::string item_style = "width:" + column_width(window.columns)
            + "%;height:" + std::to_string(window.row_height) + "px";
        for (int row = window.range.start; row < window.range.end; ++row) {
            markup += "<div class=\"managed_list_grid_row\" style=\"height:";
            markup += std::to_string(window.row_height);
            markup += "px\">";
            const int first = row * window.columns;
            const int end = std::min(
                first + window.columns, static_cast<int>(window.items.size()));
            for (int index = first; index < end; ++index) {
                append_item(markup, list_id, window, index,
                    "managed_list_grid_item", item_style);
            }
            markup += "</div>";
        }
    }
    append_spacer(markup, window.range.bottom_spacer_px);
    return markup;
}

int scroll_top_for_selected(const UiListWindow& window,
    int viewport_height,
    int current_scroll_top)
{
    if (window.selected_index < 0 || window.columns <= 0) {
        return current_scroll_top;
    }

    VirtualListController controller;
    controller.set_row_height(window.row_height);
    controller.set_total_rows(static_cast<int>(
        (window.items.size() + static_cast<size_t>(window.columns) - 1)
        / static_cast<size_t>(window.columns)));
    controller.set_viewport_height(viewport_height);
    controller.set_scroll_top(current_scroll_top);
    return controller.scroll_top_for_index(
        window.selected_index / window.columns);
}

void update_list_metadata(Rml::ElementDocument* document,
    std::string_view list_id,
    const UiListWindow& window)
{
    Rml::ElementList elements;
    document->GetElementsByClassName(elements, "managed_list_count");
    for (auto* element : elements) {
        if (element->GetAttribute<Rml::String>("data-list-id", "") == list_id) {
            element->SetInnerRML(std::to_string(window.items.size()));
        }
    }

    elements.clear();
    document->GetElementsByClassName(elements, "managed_list_title");
    for (auto* element : elements) {
        if (element->GetAttribute<Rml::String>("data-list-id", "") == list_id) {
            element->SetInnerRML(escape_markup(window.title));
        }
    }

    elements.clear();
    document->GetElementsByClassName(elements, "managed_list_panel");
    for (auto* element : elements) {
        if (element->GetAttribute<Rml::String>("data-list-id", "") == list_id) {
            element->SetClass("active", window.visible);
        }
    }
}

} // namespace

bool position_managed_list_popups(Rml::ElementDocument* document)
{
    // RmlUi owns the document tree and exposes synchronous traversal only as
    // non-owning Element pointers. An index table would duplicate that tree
    // and still require pointer lookup, so this pass keeps the pointers only
    // for the duration of one document update.
    if (!document) {
        return false;
    }

    Rml::ElementList popups;
    document->GetElementsByClassName(popups, "managed_list_popup");
    if (popups.empty()) {
        return false;
    }

    Rml::ElementList rows;
    document->GetElementsByClassName(rows, "managed_list_row");
    bool changed = false;
    for (auto* popup : popups) {
        if (!popup->IsClassSet("active")) {
            continue;
        }

        const std::string anchor_list_id = popup->GetAttribute<Rml::String>(
            "data-anchor-list-id", "");
        const std::string bounds_id = popup->GetAttribute<Rml::String>(
            "data-popup-bounds-id", "");
        const auto popup_height = parse_int(popup->GetAttribute<Rml::String>(
            "data-popup-height", ""));
        const auto anchor_cell = parse_int(popup->GetAttribute<Rml::String>(
            "data-anchor-cell", "-1"));
        if (anchor_list_id.empty() || bounds_id.empty() || !popup_height
            || *popup_height <= 0 || !anchor_cell || *anchor_cell < -1) {
            continue;
        }

        Rml::Element* anchor = nullptr;
        for (auto* row : rows) {
            if (row->IsClassSet("selected")
                && row->GetAttribute<Rml::String>("data-list-id", "")
                    == anchor_list_id) {
                anchor = row;
                break;
            }
        }
        if (!anchor) {
            continue;
        }
        if (*anchor_cell >= 0) {
            Rml::ElementList cells;
            const std::string cell_class = "cell_" + std::to_string(*anchor_cell);
            anchor->GetElementsByClassName(cells, cell_class);
            if (cells.empty()) {
                continue;
            }
            anchor = cells.front();
        }

        auto* bounds = document->GetElementById(bounds_id);
        if (!bounds) {
            continue;
        }
        const VirtualComboBoxRect anchor_rect{
            .x = static_cast<int>(std::lround(
                anchor->GetAbsoluteLeft() - bounds->GetAbsoluteLeft())),
            .y = static_cast<int>(std::lround(
                anchor->GetAbsoluteTop() - bounds->GetAbsoluteTop())),
            .width = static_cast<int>(std::lround(anchor->GetOffsetWidth())),
            .height = static_cast<int>(std::lround(anchor->GetOffsetHeight())),
        };
        const VirtualComboBoxRect bounds_rect{
            .width = static_cast<int>(std::lround(std::max(
                bounds->GetClientWidth(), bounds->GetOffsetWidth()))),
            .height = static_cast<int>(std::lround(std::max(
                bounds->GetClientHeight(), bounds->GetOffsetHeight()))),
        };
        const auto placement = place_virtual_combobox_popup(
            anchor_rect, bounds_rect, *popup_height);
        if (placement.width <= 0 || placement.height <= 0) {
            continue;
        }

        const std::string signature = std::to_string(placement.left) + ":"
            + std::to_string(placement.top) + ":"
            + std::to_string(placement.width) + ":"
            + std::to_string(placement.height);
        if (popup->GetAttribute<Rml::String>("data-popup-placement", "")
            == signature) {
            continue;
        }
        popup->SetProperty("left", std::to_string(placement.left) + "px");
        popup->SetProperty("top", std::to_string(placement.top) + "px");
        popup->SetProperty("width", std::to_string(placement.width) + "px");
        popup->SetProperty("height", std::to_string(placement.height) + "px");
        popup->SetAttribute("data-popup-placement", signature);
        changed = true;
    }
    return changed;
}

std::string render_managed_list_window(std::string_view list_id,
    const UiListWindow& window,
    std::string_view empty_text)
{
    return render_window(list_id, window, empty_text);
}

bool sync_managed_lists(Rml::ElementDocument* document,
    VirtualListHost& host,
    ManagedListRenderState& render_state,
    bool force)
{
    if (!document) {
        return false;
    }

    if (render_state.host_generation != host.generation()) {
        render_state.lists.clear();
        render_state.host_generation = host.generation();
        force = true;
    }

    Rml::ElementList row_elements;
    document->GetElementsByClassName(row_elements, "managed_list_rows");
    bool changed = false;
    for (auto* element : row_elements) {
        const std::string list_id = element->GetAttribute<Rml::String>("data-list-id", "");
        if (list_id.empty()) {
            continue;
        }
        const int viewport_height = std::max(1,
            static_cast<int>(std::lround(std::max(
                element->GetClientHeight(), element->GetOffsetHeight()))));
        const int observed_scroll_top = std::max(0,
            static_cast<int>(std::lround(element->GetScrollTop())));
        auto& record = render_state.lists[list_id];
        int scroll_top = observed_scroll_top;
        if (record.requested_scroll_top >= 0) {
            if (record.requested_scroll_top == observed_scroll_top) {
                record.requested_scroll_top = -1;
            } else {
                scroll_top = record.requested_scroll_top;
            }
        }
        auto window = host.window(list_id, viewport_height, scroll_top);
        if (!window) {
            continue;
        }

        const bool source_or_selection_changed = force || !record.rendered
            || record.revision != window->revision;
        if (source_or_selection_changed
            && element->GetAttribute<Rml::String>(
                   "data-scroll-selected", "false")
                == "true") {
            const int selected_scroll_top = scroll_top_for_selected(
                *window, viewport_height, scroll_top);
            if (selected_scroll_top != scroll_top) {
                scroll_top = selected_scroll_top;
                window = host.window(
                    list_id, viewport_height, scroll_top);
                if (!window) {
                    continue;
                }
            }
        }
        const bool request_scroll = scroll_top != observed_scroll_top;
        const int row_count = static_cast<int>(window->items.size());
        const bool replace = force || !record.rendered
            || record.revision != window->revision
            || record.row_count != row_count
            || record.range.start != window->range.start
            || record.range.end != window->range.end;
        if (replace) {
            const std::string empty_text = element->GetAttribute<Rml::String>(
                "data-empty-text", "No rows.");
            element->SetInnerRML(
                render_managed_list_window(list_id, *window, empty_text));
            element->SetScrollTop(static_cast<float>(scroll_top));
            record.range = window->range;
            record.revision = window->revision;
            record.row_count = row_count;
            record.rendered = true;
            update_list_metadata(document, list_id, *window);
            changed = true;
        }
        if (request_scroll) {
            if (!replace) {
                element->SetScrollTop(static_cast<float>(scroll_top));
                record.requested_scroll_top = -1;
            } else {
                record.requested_scroll_top = scroll_top;
            }
            changed = true;
        }
    }
    return position_managed_list_popups(document) || changed;
}

bool activate_managed_list_element(Rml::Element* element, VirtualListHost& host)
{
    auto* list = ancestor_with_class(element, "managed_list_rows");
    auto* cell = ancestor_with_class(element, "managed_list_cell");
    auto* row = ancestor_with_class(element, "managed_list_row");
    if (!list || !row) {
        return false;
    }

    const std::string list_id = row->GetAttribute<Rml::String>("data-list-id", "");
    const auto index = parse_int(row->GetAttribute<Rml::String>("data-index", ""));
    if (list_id.empty() || !index) {
        return false;
    }
    int cell_index = -1;
    if (cell) {
        const auto parsed_cell = parse_int(
            cell->GetAttribute<Rml::String>("data-cell", ""));
        if (!parsed_cell) {
            return false;
        }
        cell_index = *parsed_cell;
    }
    return host.push_activate(list_id, *index, cell_index);
}

std::optional<ManagedListFocusTarget> managed_list_focus_target(
    Rml::Element* element)
{
    auto* list = ancestor_with_class(element, "managed_list_rows");
    if (!list) {
        return std::nullopt;
    }

    const std::string element_id = list->GetAttribute<Rml::String>(
        "data-focus-after-activate", "");
    const auto cell = parse_int(list->GetAttribute<Rml::String>(
        "data-focus-cell", "-1"));
    if (element_id.empty() || !cell || *cell < -1) {
        return std::nullopt;
    }
    return ManagedListFocusTarget{
        .element_id = element_id,
        .cell = *cell,
    };
}

bool focus_managed_list_target(Rml::ElementDocument* document,
    const ManagedListFocusTarget& target)
{
    if (!document || target.element_id.empty() || target.cell < -1) {
        return false;
    }

    auto* focus = document->GetElementById(target.element_id);
    if (!focus) {
        return false;
    }
    if (target.cell >= 0) {
        Rml::ElementList rows;
        focus->GetElementsByClassName(rows, "managed_list_row");
        const auto selected = std::ranges::find_if(rows,
            [](const Rml::Element* row) {
                return row->IsClassSet("selected");
            });
        if (selected == rows.end()) {
            return false;
        }

        Rml::ElementList cells;
        const std::string cell_class = "cell_" + std::to_string(target.cell);
        (*selected)->GetElementsByClassName(cells, cell_class);
        if (cells.empty()) {
            return false;
        }
        focus = cells.front();
    }

    focus->SetAttribute("tabindex", "0");
    focus->Focus();
    return true;
}

bool cycle_managed_list_element(
    Rml::Element* element, VirtualListHost& host, int delta)
{
    auto* list = ancestor_with_class(element, "managed_list_cycle");
    if (!list || delta == 0) {
        return false;
    }
    const std::string source_list_id = list->GetAttribute<Rml::String>(
        "data-list-id", "");
    const std::string list_id = list->GetAttribute<Rml::String>(
        "data-cycle-list-id", source_list_id);
    if (list_id.empty()) {
        return false;
    }

    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(
            list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    const auto next_scroll = host.move_and_activate(
        list_id, delta, viewport_height, scroll_top);
    if (!next_scroll) {
        return false;
    }
    if (list_id == source_list_id) {
        list->SetScrollTop(static_cast<float>(*next_scroll));
    }
    return true;
}

} // namespace nw::toolset
