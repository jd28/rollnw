#pragma once

#include "../../serialization/Archives.hpp"
#include "../ObjectBase.hpp"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace nw {

struct Item;

struct InventoryItem {
    bool infinite = false;
    uint16_t pos_x;
    uint16_t pos_y;
    std::variant<Resref, std::unique_ptr<Item>> item;
};

struct Inventory {
    Inventory() = default;
    Inventory(ObjectBase* owner_)
        : owner{owner_}
    {
    }

    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    ObjectBase* owner = nullptr;
    std::vector<InventoryItem> items;
};

} // namespace nw
