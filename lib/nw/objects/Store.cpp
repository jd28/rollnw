#include "Store.hpp"

#include <nlohmann/json.hpp>

namespace nw {

StoreInventory::StoreInventory(ObjectBase* owner)
    : armor{owner}
    , miscellaneous{owner}
    , potions{owner}
    , rings{owner}
    , weapons{owner}
{
}

void StoreInventory::set_owner(ObjectBase* owner)
{
    armor.owner = owner;
    miscellaneous.owner = owner;
    potions.owner = owner;
    rings.owner = owner;
    weapons.owner = owner;
}

bool Store::instantiate()
{
    return instantiated_ = (inventory.armor.instantiate()
               && inventory.miscellaneous.instantiate()
               && inventory.potions.instantiate()
               && inventory.rings.instantiate()
               && inventory.weapons.instantiate());
}

Store::Store()
{
    set_handle({object_invalid, ObjectType::store, 0});
    inventory.set_owner(this);
}

bool Store::deserialize(Store* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    obj->common.from_json(archive.at("common"), profile, ObjectType::store);

    obj->inventory.armor.from_json(archive.at("armor"), profile);
    obj->inventory.miscellaneous.from_json(archive.at("miscellaneous"), profile);
    obj->inventory.potions.from_json(archive.at("potions"), profile);
    obj->inventory.rings.from_json(archive.at("rings"), profile);
    obj->inventory.weapons.from_json(archive.at("weapons"), profile);
    archive.at("will_not_buy").get_to(obj->inventory.will_not_buy);
    archive.at("will_only_buy").get_to(obj->inventory.will_only_buy);

    archive.at("scripts").at("on_closed").get_to(obj->scripts.on_closed);
    archive.at("scripts").at("on_opened").get_to(obj->scripts.on_opened);

    archive.at("blackmarket_markdown").get_to(obj->blackmarket_markdown);
    archive.at("identify_price").get_to(obj->identify_price);
    archive.at("markdown").get_to(obj->markdown);
    archive.at("markup").get_to(obj->markup);
    archive.at("max_price").get_to(obj->max_price);
    archive.at("gold").get_to(obj->gold);

    archive.at("blackmarket").get_to(obj->blackmarket);

    return true;
}

bool Store::serialize(const Store* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTM";
    archive["$version"] = json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::store);
    archive["scripts"] = {
        {"on_closed", obj->scripts.on_closed},
        {"on_opened", obj->scripts.on_opened},
    };

    archive["blackmarket_markdown"] = obj->blackmarket_markdown;
    archive["identify_price"] = obj->identify_price;
    archive["markdown"] = obj->markdown;
    archive["markup"] = obj->markup;
    archive["max_price"] = obj->max_price;
    archive["gold"] = obj->gold;

    archive["blackmarket"] = obj->blackmarket;

    archive["armor"] = obj->inventory.armor.to_json(profile);
    archive["miscellaneous"] = obj->inventory.miscellaneous.to_json(profile);
    archive["potions"] = obj->inventory.potions.to_json(profile);
    archive["rings"] = obj->inventory.rings.to_json(profile);
    archive["weapons"] = obj->inventory.weapons.to_json(profile);
    archive["will_not_buy"] = obj->inventory.will_not_buy;
    archive["will_only_buy"] = obj->inventory.will_only_buy;

    return true;
}

// == Store - Serialization - Gff =============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Store* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        return false;
    }
    if (!deserialize(obj->common, archive, profile, ObjectType::store)) {
        return false;
    }

    if (profile == SerializationProfile::blueprint) {
        archive.get_to("ID", obj->common.palette_id);
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
                deserialize(obj->inventory.armor, st, profile);
                break;
            case 1:
                deserialize(obj->inventory.miscellaneous, st, profile);
                break;
            case 2:
                deserialize(obj->inventory.potions, st, profile);
                break;
            case 3:
                deserialize(obj->inventory.rings, st, profile);
                break;
            case 4:
                deserialize(obj->inventory.weapons, st, profile);
                break;
            }
        }
    }

    archive.get_to("OnOpenStore", obj->scripts.on_opened);
    archive.get_to("OnStoreClosed", obj->scripts.on_closed);

    sz = archive["WillNotBuy"].size();
    obj->inventory.will_not_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!archive["WillNotBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            obj->inventory.will_not_buy.push_back(base);
        }
    }

    sz = archive["WillOnlyBuy"].size();
    obj->inventory.will_only_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!archive["WillOnlyBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            obj->inventory.will_only_buy.push_back(base);
        }
    }

    archive.get_to("BM_MarkDown", obj->blackmarket_markdown);
    archive.get_to("IdentifyPrice", obj->identify_price);
    archive.get_to("MarkDown", obj->markdown);
    archive.get_to("MarkUp", obj->markup);
    archive.get_to("MaxBuyPrice", obj->max_price);
    archive.get_to("StoreGold", obj->gold);

    archive.get_to("BlackMarket", obj->blackmarket);

    return true;
}

bool serialize(const Store* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("ResRef", obj->common.resref) // Store does it's own thing, not typo.
        .add_field("LocName", obj->common.name)
        .add_field("Tag", obj->common.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj->common.comment);
        archive.add_field("ID", obj->common.palette_id);
    } else {
        archive.add_field("PositionX", obj->common.location.position.x)
            .add_field("PositionY", obj->common.location.position.y)
            .add_field("PositionZ", obj->common.location.position.z)
            .add_field("OrientationX", obj->common.location.orientation.x)
            .add_field("OrientationY", obj->common.location.orientation.y);
    }

    if (obj->common.locals.size()) {
        serialize(obj->common.locals, archive, profile);
    }

    auto& store_list = archive.add_list("StoreList");
    serialize(obj->inventory.armor, store_list.push_back(0), profile);
    serialize(obj->inventory.miscellaneous, store_list.push_back(1), profile);
    serialize(obj->inventory.potions, store_list.push_back(2), profile);
    serialize(obj->inventory.rings, store_list.push_back(3), profile);
    serialize(obj->inventory.weapons, store_list.push_back(4), profile);

    auto& wnb_list = archive.add_list("WillNotBuy");
    for (const auto bi : obj->inventory.will_not_buy) {
        wnb_list.push_back(0x17E4D).add_field("BaseItem", bi);
    }

    auto& wob_list = archive.add_list("WillOnlyBuy");
    for (const auto bi : obj->inventory.will_only_buy) {
        wob_list.push_back(0x17E4D).add_field("BaseItem", bi);
    }

    archive.add_field("OnOpenStore", obj->scripts.on_opened)
        .add_field("OnStoreClosed", obj->scripts.on_closed);

    archive.add_field("BM_MarkDown", obj->blackmarket_markdown)
        .add_field("IdentifyPrice", obj->identify_price)
        .add_field("MarkDown", obj->markdown)
        .add_field("MarkUp", obj->markup)
        .add_field("MaxBuyPrice", obj->max_price)
        .add_field("StoreGold", obj->gold);

    archive.add_field("BlackMarket", obj->blackmarket);

    return true;
}

GffBuilder serialize(const Store* obj, SerializationProfile profile)
{
    GffBuilder out{"UTS"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
