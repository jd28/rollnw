#include "Store.hpp"

namespace nw {

Store::Store(const GffInputArchiveStruct gff, SerializationProfile profile)
    : common_{ObjectType::store, gff, profile}
{

    gff.get_to("BlackMarket", blackmarket);
    gff.get_to("BM_MarkDown", blackmarket_markdown);
    gff.get_to("IdentifyPrice", identify_price);
    gff.get_to("MarkDown", markdown);
    gff.get_to("MarkUp", markup);
    gff.get_to("MaxBuyPrice", max_price);
    gff.get_to("OnOpenStore", on_opened);
    gff.get_to("OnStoreClosed", on_closed);
    gff.get_to("StoreGold", gold);

    size_t sz = gff["WillNotBuy"].size();
    will_not_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!gff["WillNotBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            will_not_buy.push_back(base);
        }
    }

    sz = gff["WillOnlyBuy"].size();
    will_only_buy.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        int32_t base;
        if (!gff["WillOnlyBuy"][i].get_to("BaseItem", base)) {
            LOG_F(ERROR, "store will not buy, invalid base item at index {}", i);
            break;
        } else {
            will_only_buy.push_back(base);
        }
    }

    sz = gff["StoreList"].size();
    if (sz != 5) {
        valid_ = false;
        LOG_F(ERROR, "store invalid number of store containers");
    } else {
        for (size_t i = 0; i < sz; ++i) {
            auto st = gff["StoreList"][i];
            switch (st.id()) {
            default:
                LOG_F(ERROR, "store invalid store container id: {}", st.id());
                break;
            case 0:
                armor = Inventory{st, profile};
                break;
            case 1:
                miscellaneous = Inventory{st, profile};
                break;
            case 2:
                potions = Inventory{st, profile};
                break;
            case 3:
                rings = Inventory{st, profile};
                break;
            case 4:
                weapons = Inventory{st, profile};
                break;
            }
        }
    }
}

} // namespace nw
