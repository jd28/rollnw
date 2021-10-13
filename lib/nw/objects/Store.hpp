#pragma once

#include "../formats/Gff.hpp"
#include "Item.hpp"
#include "Object.hpp"
#include "components/Inventory.hpp"

#include <vector>

namespace nw {

struct Store : public Object {
    Store(const GffStruct gff, SerializationProfile profile);

    Resref on_closed;
    Resref on_opened;

    int32_t blackmarket_markdown = 0;
    int32_t identify_price = 100;
    int32_t markdown = 0;
    int32_t markup = 0;
    int32_t max_price = -1;
    int32_t gold = -1;

    std::vector<int32_t> will_not_buy;
    std::vector<int32_t> will_only_buy;

    uint8_t blackmarket;

    Inventory armor;
    Inventory miscellaneous;
    Inventory potions;
    Inventory rings;
    Inventory weapons;
};

} // namespace nw
