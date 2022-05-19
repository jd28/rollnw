#pragma once

#include "../rules/BaseItem.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"
#include "components/LocalData.hpp"
#include "components/Location.hpp"

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
    Item(Item&&) = default;
    Item(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Item(const nlohmann::json& archive, SerializationProfile profile);
    ~Item() = default;

    Item& operator=(Item&&) = default;

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::item;

    // ObjectBase overrides
    virtual bool valid() const noexcept override;
    virtual Common* common() override;
    virtual const Common* common() const override;
    virtual bool instantiate() override;
    virtual Item* as_item() override;
    virtual const Item* as_item() const override;

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    GffOutputArchive to_gff(SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;

    LocString description;
    LocString description_id;
    Inventory inventory;
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

private:
    bool valid_ = false;
};

} // namespace nw
