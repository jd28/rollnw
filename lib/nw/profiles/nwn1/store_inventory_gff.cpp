#include "store_inventory_gff.hpp"

#include "inventory_component_gff.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/Inventory.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../objects/Store.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"

namespace nwn1 {

namespace {

constexpr size_t store_inventory_bucket_count = 5;
constexpr uint32_t store_buy_filter_struct_id = 0x17E4D;

void serialize_empty_store_inventory(nw::GffBuilderStruct& archive)
{
    auto& store_list = archive.add_list("StoreList");
    store_list.push_back(0);
    store_list.push_back(1);
    store_list.push_back(2);
    store_list.push_back(3);
    store_list.push_back(4);

    archive.add_list("WillNotBuy");
    archive.add_list("WillOnlyBuy");
}

bool import_store_bucket(
    nw::StoreInventory& inventory,
    const nw::GffStruct& bucket,
    nw::SerializationProfile profile)
{
    switch (bucket.id()) {
    default:
        LOG_F(ERROR, "store invalid store container id: {}", bucket.id());
        return true;
    case 0:
        return import_inventory_items_from_gff(inventory.armor, bucket, profile);
    case 1:
        return import_inventory_items_from_gff(inventory.miscellaneous, bucket, profile);
    case 2:
        return import_inventory_items_from_gff(inventory.potions, bucket, profile);
    case 3:
        return import_inventory_items_from_gff(inventory.rings, bucket, profile);
    case 4:
        return import_inventory_items_from_gff(inventory.weapons, bucket, profile);
    }
}

void import_buy_filter(
    nw::Vector<int32_t>& out,
    const nw::GffField& list,
    const char* label)
{
    const size_t sz = list.size();
    out.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!list[i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store {}, invalid base item at index {}", label, i);
            break;
        }
        out.push_back(base);
    }
}

bool serialize_store_inventory(
    const nw::StoreInventory& inventory,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    auto& store_list = archive.add_list("StoreList");
    if (!export_inventory_items_to_gff(inventory.armor, store_list.push_back(0), profile)) { return false; }
    if (!export_inventory_items_to_gff(inventory.miscellaneous, store_list.push_back(1), profile)) { return false; }
    if (!export_inventory_items_to_gff(inventory.potions, store_list.push_back(2), profile)) { return false; }
    if (!export_inventory_items_to_gff(inventory.rings, store_list.push_back(3), profile)) { return false; }
    if (!export_inventory_items_to_gff(inventory.weapons, store_list.push_back(4), profile)) { return false; }

    auto& wnb_list = archive.add_list("WillNotBuy");
    for (const auto bi : inventory.will_not_buy) {
        wnb_list.push_back(store_buy_filter_struct_id).add_field("BaseItem", bi);
    }

    auto& wob_list = archive.add_list("WillOnlyBuy");
    for (const auto bi : inventory.will_only_buy) {
        wob_list.push_back(store_buy_filter_struct_id).add_field("BaseItem", bi);
    }
    return true;
}

} // namespace

bool import_store_inventory_from_gff(
    nw::Store& store,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile)
{
    const size_t sz = archive["StoreList"].size();
    if (sz != store_inventory_bucket_count) {
        LOG_F(ERROR, "store invalid number of store containers");
        return false;
    }

    auto* inventory = nw::kernel::objects().components().get_or_create_store_inventory(store);
    if (!inventory) { return false; }

    for (size_t i = 0; i < sz; ++i) {
        if (!import_store_bucket(*inventory, archive["StoreList"][i], profile)) { return false; }
    }

    import_buy_filter(inventory->will_not_buy, archive["WillNotBuy"], "will not buy");
    import_buy_filter(inventory->will_only_buy, archive["WillOnlyBuy"], "will only buy");
    return true;
}

bool export_store_inventory_to_gff(
    const nw::Store& store,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    if (const auto* inventory = nw::kernel::objects().components().find_store_inventory(store)) {
        return serialize_store_inventory(*inventory, archive, profile);
    } else {
        serialize_empty_store_inventory(archive);
    }
    return true;
}

} // namespace nwn1
