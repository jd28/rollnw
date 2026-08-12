#include "rml_managed_list.hpp"

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
        const int scroll_top = std::max(0,
            static_cast<int>(std::lround(element->GetScrollTop())));
        auto window = host.window(list_id, viewport_height, scroll_top);
        if (!window) {
            continue;
        }

        auto& record = render_state.lists[list_id];
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
    }
    return changed;
}

bool activate_managed_list_element(Rml::Element* element, VirtualListHost& host)
{
    auto* cell = ancestor_with_class(element, "managed_list_cell");
    auto* row = ancestor_with_class(element, "managed_list_row");
    if (!row) {
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

bool cycle_managed_list_element(
    Rml::Element* element, VirtualListHost& host, int delta)
{
    auto* list = ancestor_with_class(element, "managed_list_cycle");
    if (!list || delta == 0) {
        return false;
    }
    const std::string list_id = list->GetAttribute<Rml::String>(
        "data-list-id", "");
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
    list->SetScrollTop(static_cast<float>(*next_scroll));
    return true;
}

} // namespace nw::toolset
