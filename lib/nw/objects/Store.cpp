#include "Store.hpp"

#include <nlohmann/json.hpp>

namespace nw {

StoreInventory::StoreInventory(flecs::entity owner)
    : armor{owner}
    , miscellaneous{owner}
    , potions{owner}
    , rings{owner}
    , weapons{owner}
{
}

// bool Store::instantiate()
// {
//     return armor.instantiate()
//         && miscellaneous.instantiate()
//         && potions.instantiate()
//         && rings.instantiate()
//         && weapons.instantiate();
// }

bool Store::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto store = ent.get_mut<Store>();
    auto inventory = ent.get_mut<StoreInventory>();
    auto scripts = ent.get_mut<StoreScripts>();
    auto common = ent.get_mut<Common>();

    if (!common->from_gff(archive, profile)) {
        return false;
    }

    if (profile == SerializationProfile::blueprint) {
        archive.get_to("ID", common->palette_id);
    }

    size_t sz = archive["StoreList"].size();
    if (sz != 5) {
        LOG_F(ERROR, "store invalid number of store containers");
        return false;
    } else {
        for (size_t i = 0; i < sz; ++i) {
            auto st = archive["StoreList"][i];
            switch (st.id()) {
            default:
                LOG_F(ERROR, "store invalid store container id: {}", st.id());
                break;
            case 0:
                inventory->armor.from_gff(st, profile);
                break;
            case 1:
                inventory->miscellaneous.from_gff(st, profile);
                break;
            case 2:
                inventory->potions.from_gff(st, profile);
                break;
            case 3:
                inventory->rings.from_gff(st, profile);
                break;
            case 4:
                inventory->weapons.from_gff(st, profile);
                break;
            }
        }
    }

    archive.get_to("OnOpenStore", scripts->on_opened);
    archive.get_to("OnStoreClosed", scripts->on_closed);

    sz = archive["WillNotBuy"].size();
    inventory->will_not_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!archive["WillNotBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            inventory->will_not_buy.push_back(base);
        }
    }

    sz = archive["WillOnlyBuy"].size();
    inventory->will_only_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!archive["WillOnlyBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            inventory->will_only_buy.push_back(base);
        }
    }

    archive.get_to("BM_MarkDown", store->blackmarket_markdown);
    archive.get_to("IdentifyPrice", store->identify_price);
    archive.get_to("MarkDown", store->markdown);
    archive.get_to("MarkUp", store->markup);
    archive.get_to("MaxBuyPrice", store->max_price);
    archive.get_to("StoreGold", store->gold);

    archive.get_to("BlackMarket", store->blackmarket);

    return true;
}

bool Store::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto store = ent.get_mut<Store>();
    auto inventory = ent.get_mut<StoreInventory>();
    auto scripts = ent.get_mut<StoreScripts>();
    auto common = ent.get_mut<Common>();

    common->from_json(archive.at("common"), profile);

    inventory->armor.from_json(archive.at("armor"), profile);
    inventory->miscellaneous.from_json(archive.at("miscellaneous"), profile);
    inventory->potions.from_json(archive.at("potions"), profile);
    inventory->rings.from_json(archive.at("rings"), profile);
    inventory->weapons.from_json(archive.at("weapons"), profile);
    archive.at("will_not_buy").get_to(inventory->will_not_buy);
    archive.at("will_only_buy").get_to(inventory->will_only_buy);

    archive.at("scripts").at("on_closed").get_to(scripts->on_closed);
    archive.at("scripts").at("on_opened").get_to(scripts->on_opened);

    archive.at("blackmarket_markdown").get_to(store->blackmarket_markdown);
    archive.at("identify_price").get_to(store->identify_price);
    archive.at("markdown").get_to(store->markdown);
    archive.at("markup").get_to(store->markup);
    archive.at("max_price").get_to(store->max_price);
    archive.at("gold").get_to(store->gold);

    archive.at("blackmarket").get_to(store->blackmarket);

    return true;
}

bool Store::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto store = ent.get<Store>();
    auto inventory = ent.get<StoreInventory>();
    auto scripts = ent.get<StoreScripts>();
    auto common = ent.get<Common>();

    archive.add_field("ResRef", common->resref) // Store does it's own thing, not typo.
        .add_field("LocName", common->name)
        .add_field("Tag", common->tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common->comment);
        archive.add_field("ID", common->palette_id);
    } else {
        archive.add_field("PositionX", common->location.position.x)
            .add_field("PositionY", common->location.position.y)
            .add_field("PositionZ", common->location.position.z)
            .add_field("OrientationX", common->location.orientation.x)
            .add_field("OrientationY", common->location.orientation.y);
    }

    if (common->locals.size()) {
        common->locals.to_gff(archive);
    }

    auto& store_list = archive.add_list("StoreList");
    inventory->armor.to_gff(store_list.push_back(0), profile);
    inventory->miscellaneous.to_gff(store_list.push_back(1), profile);
    inventory->potions.to_gff(store_list.push_back(2), profile);
    inventory->rings.to_gff(store_list.push_back(3), profile);
    inventory->weapons.to_gff(store_list.push_back(4), profile);

    auto& wnb_list = archive.add_list("WillNotBuy");
    for (const auto bi : inventory->will_not_buy) {
        wnb_list.push_back(0x17E4D).add_field("BaseItem", bi);
    }

    auto& wob_list = archive.add_list("WillOnlyBuy");
    for (const auto bi : inventory->will_only_buy) {
        wob_list.push_back(0x17E4D).add_field("BaseItem", bi);
    }

    archive.add_field("OnOpenStore", scripts->on_opened)
        .add_field("OnStoreClosed", scripts->on_closed);

    archive.add_field("BM_MarkDown", store->blackmarket_markdown)
        .add_field("IdentifyPrice", store->identify_price)
        .add_field("MarkDown", store->markdown)
        .add_field("MarkUp", store->markup)
        .add_field("MaxBuyPrice", store->max_price)
        .add_field("StoreGold", store->gold);

    archive.add_field("BlackMarket", store->blackmarket);

    return true;
}

GffOutputArchive Store::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTS"};
    Store::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Store::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto store = ent.get<Store>();
    auto inventory = ent.get<StoreInventory>();
    auto scripts = ent.get<StoreScripts>();
    auto common = ent.get<Common>();

    archive["$type"] = "UTM";
    archive["$version"] = json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["scripts"] = {
        {"on_closed", scripts->on_closed},
        {"on_opened", scripts->on_opened},
    };

    archive["blackmarket_markdown"] = store->blackmarket_markdown;
    archive["identify_price"] = store->identify_price;
    archive["markdown"] = store->markdown;
    archive["markup"] = store->markup;
    archive["max_price"] = store->max_price;
    archive["gold"] = store->gold;

    archive["blackmarket"] = store->blackmarket;

    archive["armor"] = inventory->armor.to_json(profile);
    archive["miscellaneous"] = inventory->miscellaneous.to_json(profile);
    archive["potions"] = inventory->potions.to_json(profile);
    archive["rings"] = inventory->rings.to_json(profile);
    archive["weapons"] = inventory->weapons.to_json(profile);
    archive["will_not_buy"] = inventory->will_not_buy;
    archive["will_only_buy"] = inventory->will_only_buy;

    return true;
}

} // namespace nw
