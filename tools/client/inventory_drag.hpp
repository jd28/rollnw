#pragma once

#include <nw/objects/Inventory.hpp>

namespace Rml {
class Element;
class ElementDocument;
class Event;
}

namespace nw::toolset {

// One cursor gesture; offsets preserve the cell grabbed within an item.
struct InventoryDragGeometry {
    int width = 0;
    int height = 0;
    int column_offset = 0;
    int row_offset = 0;
};

bool begin_inventory_drag(InventoryDragGeometry& geometry, ObjectHandle item,
    Rml::Element& source, const Rml::Event& event, bool carried);
InventorySlot inventory_drag_cell(const InventoryDragGeometry& geometry,
    Rml::Element& board, const Rml::Event& event, int page);
void show_inventory_drag_cells(Rml::Element& board, const InventoryDragGeometry& geometry,
    InventorySlot cell, bool allowed);
void clear_inventory_drag_feedback(Rml::ElementDocument* doc);

} // namespace nw::toolset
