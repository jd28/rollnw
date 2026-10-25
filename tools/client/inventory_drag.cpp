#include "inventory_drag.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace nw::toolset {

bool begin_inventory_drag(InventoryDragGeometry& geometry, ObjectHandle item,
    Rml::Element& source, const Rml::Event& event, bool carried)
{
    const auto* layout = kernel::objects().components().find_item_layout(item);
    if (!layout || layout->inventory_width <= 0 || layout->inventory_width > Inventory::max_columns
        || layout->inventory_height <= 0 || layout->inventory_height > Inventory::max_rows) { return false; }
    geometry = {layout->inventory_width, layout->inventory_height};
    if (carried) {
        const float cell_size = 32 * Rml::ElementUtilities::GetDensityIndependentPixelRatio(&source);
        geometry.column_offset = std::clamp(static_cast<int>((event.GetParameter<float>("mouse_x", 0) - source.GetAbsoluteLeft()) / cell_size), 0, geometry.width - 1);
        geometry.row_offset = std::clamp(static_cast<int>((event.GetParameter<float>("mouse_y", 0) - source.GetAbsoluteTop()) / cell_size), 0, geometry.height - 1);
    } else {
        geometry.column_offset = geometry.width / 2;
        geometry.row_offset = geometry.height / 2;
    }
    return true;
}

InventorySlot inventory_drag_cell(const InventoryDragGeometry& geometry,
    Rml::Element& board, const Rml::Event& event, int page)
{
    const float cell_size = 32 * Rml::ElementUtilities::GetDensityIndependentPixelRatio(&board);
    const float x = event.GetParameter<float>("mouse_x", 0) - board.GetAbsoluteLeft() - board.GetClientLeft();
    const float y = event.GetParameter<float>("mouse_y", 0) - board.GetAbsoluteTop() - board.GetClientTop();
    return {page, static_cast<int>(std::floor(y / cell_size)) - geometry.row_offset + geometry.height - 1,
        static_cast<int>(std::floor(x / cell_size)) - geometry.column_offset};
}

void show_inventory_drag_cells(Rml::Element& board, const InventoryDragGeometry& geometry,
    InventorySlot cell, bool allowed)
{
    auto* outline = board.QuerySelector(".inventory_drop_outline");
    if (!outline) { return; }
    outline->SetProperty("left", fmt::format("{}dp", cell.col * 32));
    outline->SetProperty("top", fmt::format("{}dp", (cell.row - geometry.height + 1) * 32));
    outline->SetProperty("width", fmt::format("{}dp", geometry.width * 32));
    outline->SetProperty("height", fmt::format("{}dp", geometry.height * 32));
    board.SetClass("inventory_drop_target", allowed);
    board.SetClass("inventory_drop_invalid", !allowed);
}

void clear_inventory_drag_feedback(Rml::ElementDocument* doc)
{
    if (!doc) { return; }
    Rml::ElementList elements;
    doc->QuerySelectorAll(elements, ".inventory_drag_source, .inventory_drop_target, .inventory_drop_invalid");
    for (auto* element : elements) {
        element->SetClass("inventory_drag_source", false);
        element->SetClass("inventory_drop_target", false);
        element->SetClass("inventory_drop_invalid", false);
    }
}

} // namespace nw::toolset
