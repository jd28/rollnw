#pragma once

#include "../../serialization/Archives.hpp"

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

    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    nlohmann::json to_json(SerializationProfile profile) const;

    std::vector<InventoryItem> items;
};

} // namespace nw
