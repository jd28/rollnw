#include "inventory_component_gff.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/Inventory.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"

namespace nwn1 {

namespace {

bool inventory_gff_present(
    const nw::GffStruct& archive,
    InventoryGffPresence presence)
{
    if (archive["ItemList"].size() > 0) { return true; }

    if (presence == InventoryGffPresence::has_inventory_or_item_list) {
        bool has_inventory = false;
        archive.get_to("HasInventory", has_inventory, false);
        return has_inventory;
    }

    return false;
}

} // namespace

bool import_inventory_items_from_gff(
    nw::Inventory& inventory,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile)
{
    const size_t sz = archive["ItemList"].size();
    for (size_t i = 0; i < sz; ++i) {
        bool valid_entry = true;
        auto st = archive["ItemList"][i];
        nw::InventoryItem ii;
        auto handle = inventory.owner->handle();
        if (handle.type == nw::ObjectType::store) {
            st.get_to("Infinite", ii.infinite, false);
        }
        st.get_to("Repos_PosX", ii.pos_x);
        st.get_to("Repos_Posy", ii.pos_y); // Not typo.

        if (nw::SerializationProfile::blueprint == profile) {
            if (auto r = st.get<nw::Resref>("InventoryRes")) {
                ii.item = *r;
            }
        } else if (nw::SerializationProfile::instance == profile) {
            auto temp = nw::kernel::objects().load_instance<nw::Item>(st);
            if (temp) {
                ii.item = temp->handle();
            } else {
                valid_entry = false;
            }
        }
        if (valid_entry) {
            inventory.items.push_back(std::move(ii));
        }
    }

    return true;
}

bool export_inventory_items_to_gff(
    const nw::Inventory& inventory,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    if (inventory.items.empty()) { return true; }

    auto& list = archive.add_list("ItemList");
    for (const auto& it : inventory.items) {
        auto* item = nw::inventory_item_ptr(it);
        if (!item && !(nw::SerializationProfile::blueprint == profile && it.item.is<nw::Resref>())) {
            continue;
        }

        auto& str = list.push_back(static_cast<uint32_t>(list.size()))
                        .add_field("Repos_PosX", it.pos_x)
                        .add_field("Repos_Posy", it.pos_y);

        auto handle = inventory.owner->handle();
        if (handle.type == nw::ObjectType::store && it.infinite) {
            str.add_field("Infinite", it.infinite);
        }

        if (nw::SerializationProfile::blueprint == profile) {
            if (it.item.is<nw::Resref>()) {
                str.add_field("InventoryRes", it.item.as<nw::Resref>());
            } else if (item) {
                str.add_field("InventoryRes", item->resref);
            }
        } else if (item) {
            nw::serialize(item, str, profile);
        }
    }
    return true;
}

bool import_inventory_component_from_gff(
    nw::ObjectBase& owner,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile,
    int pages,
    int rows,
    int columns,
    InventoryGffPresence presence)
{
    if (!inventory_gff_present(archive, presence)) { return true; }

    auto* inventory = nw::kernel::objects().components().get_or_create_inventory(owner, pages, rows, columns);
    if (!inventory) { return false; }

    return import_inventory_items_from_gff(*inventory, archive, profile);
}

bool export_inventory_component_to_gff(
    const nw::ObjectBase& owner,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    if (const auto* inventory = nw::kernel::objects().components().find_inventory(owner)) {
        return export_inventory_items_to_gff(*inventory, archive, profile);
    }
    return true;
}

} // namespace nwn1
