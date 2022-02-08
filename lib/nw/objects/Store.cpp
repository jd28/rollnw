#include "Store.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Store::Store()
    : common_{ObjectType::store}
    , armor{this}
    , miscellaneous{this}
    , potions{this}
    , rings{this}
    , weapons{this}
{
}

Store::Store(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::store}
    , armor{this}
    , miscellaneous{this}
    , potions{this}
    , rings{this}
    , weapons{this}
{
    valid_ = this->from_gff(archive, profile);
}

Store::Store(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::store}
    , armor{this}
    , miscellaneous{this}
    , potions{this}
    , rings{this}
    , weapons{this}
{
    valid_ = this->from_json(archive, profile);
}

bool Store::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    if (!common_.from_gff(archive, profile)) {
        return false;
    }

    if (profile == SerializationProfile::blueprint) {
        archive.get_to("ID", common_.palette_id);
    }

    archive.get_to("BlackMarket", blackmarket);
    archive.get_to("BM_MarkDown", blackmarket_markdown);
    archive.get_to("IdentifyPrice", identify_price);
    archive.get_to("MarkDown", markdown);
    archive.get_to("MarkUp", markup);
    archive.get_to("MaxBuyPrice", max_price);
    archive.get_to("OnOpenStore", on_opened);
    archive.get_to("OnStoreClosed", on_closed);
    archive.get_to("StoreGold", gold);

    size_t sz = archive["WillNotBuy"].size();
    will_not_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!archive["WillNotBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            will_not_buy.push_back(base);
        }
    }

    sz = archive["WillOnlyBuy"].size();
    will_only_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!archive["WillOnlyBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            will_only_buy.push_back(base);
        }
    }

    sz = archive["StoreList"].size();
    if (sz != 5) {
        valid_ = false;
        LOG_F(ERROR, "store invalid number of store containers");
    } else {
        for (size_t i = 0; i < sz; ++i) {
            auto st = archive["StoreList"][i];
            switch (st.id()) {
            default:
                LOG_F(ERROR, "store invalid store container id: {}", st.id());
                break;
            case 0:
                armor.from_gff(st, profile);
                break;
            case 1:
                miscellaneous.from_gff(st, profile);
                break;
            case 2:
                potions.from_gff(st, profile);
                break;
            case 3:
                rings.from_gff(st, profile);
                break;
            case 4:
                weapons.from_gff(st, profile);
                break;
            }
        }
    }
    return true;
}

bool Store::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    common_.from_json(archive.at("common"), profile);

    archive.at("on_closed").get_to(on_closed);
    archive.at("on_opened").get_to(on_opened);

    archive.at("blackmarket_markdown").get_to(blackmarket_markdown);
    archive.at("identify_price").get_to(identify_price);
    archive.at("markdown").get_to(markdown);
    archive.at("markup").get_to(markup);
    archive.at("max_price").get_to(max_price);
    archive.at("gold").get_to(gold);

    archive.at("will_not_buy").get_to(will_not_buy);
    archive.at("will_only_buy").get_to(will_only_buy);

    archive.at("blackmarket").get_to(blackmarket);

    armor.from_json(archive.at("armor"), profile);
    miscellaneous.from_json(archive.at("miscellaneous"), profile);
    potions.from_json(archive.at("potions"), profile);
    rings.from_json(archive.at("rings"), profile);
    weapons.from_json(archive.at("weapons"), profile);

    return true;
}

bool Store::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    archive.add_field("ResRef", common_.resref) // Store does it's own thing, not typo.
        .add_field("LocName", common_.name)
        .add_field("Tag", common_.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("ID", common_.palette_id);
    } else {
        archive.add_field("PositionX", common_.location.position.x)
            .add_field("PositionY", common_.location.position.y)
            .add_field("PositionZ", common_.location.position.z)
            .add_field("OrientationX", common_.location.orientation.x)
            .add_field("OrientationY", common_.location.orientation.y);
    }

    if (common_.locals.size()) {
        common_.locals.to_gff(archive);
    }

    archive.add_field("BlackMarket", blackmarket)
        .add_field("BM_MarkDown", blackmarket_markdown)
        .add_field("IdentifyPrice", identify_price)
        .add_field("MarkDown", markdown)
        .add_field("MarkUp", markup)
        .add_field("MaxBuyPrice", max_price)
        .add_field("OnOpenStore", on_opened)
        .add_field("OnStoreClosed", on_closed)
        .add_field("StoreGold", gold);

    auto& wnb_list = archive.add_list("WillNotBuy");
    for (const auto bi : will_not_buy) {
        wnb_list.push_back(0x17E4D).add_field("BaseItem", bi);
    }

    auto& wob_list = archive.add_list("WillOnlyBuy");
    for (const auto bi : will_only_buy) {
        wob_list.push_back(0x17E4D).add_field("BaseItem", bi);
    }

    auto& store_list = archive.add_list("StoreList");
    armor.to_gff(store_list.push_back(0), profile);
    miscellaneous.to_gff(store_list.push_back(1), profile);
    potions.to_gff(store_list.push_back(2), profile);
    rings.to_gff(store_list.push_back(3), profile);
    weapons.to_gff(store_list.push_back(4), profile);

    return true;
}

GffOutputArchive Store::to_gff(SerializationProfile profile) const
{
    GffOutputArchive out{"UTS"};
    this->to_gff(out.top, profile);
    out.build();
    return out;
}

nlohmann::json Store::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTM";
    j["$version"] = json_archive_version;

    j["common"] = common_.to_json(profile);
    j["on_closed"] = on_closed;
    j["on_opened"] = on_opened;

    j["blackmarket_markdown"] = blackmarket_markdown;
    j["identify_price"] = identify_price;
    j["markdown"] = markdown;
    j["markup"] = markup;
    j["max_price"] = max_price;
    j["gold"] = gold;

    j["will_not_buy"] = will_not_buy;
    j["will_only_buy"] = will_only_buy;

    j["blackmarket"] = blackmarket;

    j["armor"] = armor.to_json(profile);
    j["miscellaneous"] = miscellaneous.to_json(profile);
    j["potions"] = potions.to_json(profile);
    j["rings"] = rings.to_json(profile);
    j["weapons"] = weapons.to_json(profile);

    return j;
}

} // namespace nw
