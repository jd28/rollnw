#include "Inventory.hpp"

#include "../Item.hpp"

namespace nw {

Inventory::Inventory(const GffStruct gff, SerializationProfile profile)
{
    size_t sz = gff["ItemList"].size();
    items.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        auto st = gff["ItemList"][i];
        InventoryItem ii;

        st.get_to("Repos_PosX", ii.pos_x);
        st.get_to("Repos_PosX", ii.pos_y);

        if (SerializationProfile::blueprint == profile) {
            if (auto r = st.get<Resref>("InventoryRes")) {
                ii.item = *r;
            }
        } else if (SerializationProfile::instance == profile) {
            ii.item = std::make_unique<Item>(st, profile);
        }

        items.push_back(std::move(ii));
    }
}

} // namespace nw
