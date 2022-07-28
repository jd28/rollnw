#pragma once

#include "../rules/BaseItem.hpp"
#include "Common.hpp"
#include "Inventory.hpp"
#include "LocalData.hpp"
#include "Location.hpp"
#include "ObjectBase.hpp"

#include <array>
#include <vector>

namespace nw {

struct ItemProperty {
    uint16_t type = 0;
    uint16_t subtype = 0;
    uint8_t cost_table = 0;
    uint16_t cost_value = 0;
    uint8_t param_table = std::numeric_limits<uint8_t>::max();
    uint8_t param_value = std::numeric_limits<uint8_t>::max();
};

struct Item : public ObjectBase {
    Item();
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::item;
    static constexpr ResourceType::type restype = ResourceType::uti;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Item* as_item() override { return this; }
    virtual const Item* as_item() const override { return this; }

    // Serialization
    static bool deserialize(Item* obj, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(Item* obj, const nlohmann::json& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const Item* obj, SerializationProfile profile);
    static bool serialize(const Item* obj, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static bool serialize(const Item* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
    Inventory inventory;

    LocString description;
    LocString description_id;
    std::vector<ItemProperty> properties;

    uint32_t cost = 0;
    uint32_t additional_cost = 0;
    int32_t baseitem;

    uint16_t stacksize = 1;

    uint8_t charges = 0;
    bool cursed = false;
    bool identified = false;
    bool plot = false;
    bool stolen = false;

    ItemModelType model_type = ItemModelType::simple;
    std::array<uint8_t, 6> model_colors;
    std::array<uint8_t, 19> model_parts;
};

} // namespace nw
