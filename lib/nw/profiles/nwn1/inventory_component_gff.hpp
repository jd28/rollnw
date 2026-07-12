#pragma once

#include "../../serialization/Serialization.hpp"

namespace nw {
struct GffBuilderStruct;
struct GffStruct;
struct Inventory;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

enum class InventoryGffPresence {
    item_list,
    has_inventory_or_item_list,
};

[[nodiscard]] bool import_inventory_items_from_gff(
    nw::Inventory& inventory,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool export_inventory_items_to_gff(
    const nw::Inventory& inventory,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool import_inventory_component_from_gff(
    nw::ObjectBase& owner,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile,
    int pages,
    int rows,
    int columns,
    InventoryGffPresence presence = InventoryGffPresence::item_list);

[[nodiscard]] bool export_inventory_component_to_gff(
    const nw::ObjectBase& owner,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile);

} // namespace nwn1
