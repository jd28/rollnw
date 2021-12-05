#pragma once

#include "../../serialization/Serialization.hpp"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace nw {

struct Item;

struct InventoryItem {
    uint16_t pos_x;
    uint16_t pos_y;
    std::variant<Resref, std::unique_ptr<Item>> item;
};

struct Inventory {
    Inventory() = default;
    Inventory(const GffInputArchiveStruct gff, SerializationProfile profile);

    std::vector<InventoryItem> items;
};

} // namespace nw
