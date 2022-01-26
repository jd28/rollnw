#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"
#include "components/LocalData.hpp"
#include "components/Location.hpp"

#include <array>
#include <vector>

namespace nw {

enum struct ItemModelType {
    simple,
    layered,
    composite,
    armor
};

struct ItemColors {
    enum type : uint8_t {
        cloth1 = 0,
        cloth2 = 1,
        leather1 = 2,
        leather2 = 3,
        metal1 = 4,
        metal2 = 5,
    };
};

struct ItemModelParts {
    enum type : uint8_t {
        model1 = 0,
        model2 = 1,
        model3 = 2,

        armor_belt = model1,
        armor_lbicep = model2,
        armor_lfarm = model3,
        armor_lfoot = 3,
        armor_lhand = 4,
        armor_lshin = 5,
        armor_lshoul = 6,
        armor_lthigh = 7,
        armor_neck = 8,
        armor_pelvis = 9,
        armor_rbicep = 10,
        armor_rfarm = 11,
        armor_rfoot = 12,
        armor_rhand = 13,
        armor_robe = 14,
        armor_rshin = 15,
        armor_rshoul = 16,
        armor_rthigh = 17,
        armor_torso = 18
    };
};

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
    Item(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Item(const nlohmann::json& archive, SerializationProfile profile);
    ~Item() = default;

    static constexpr int json_archive_version = 1;

    // ObjectBase overrides
    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Item* as_item() override { return this; }
    virtual const Item* as_item() const override { return this; }

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;

    LocString description;
    LocString description_id;
    Inventory inventory;

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
    std::vector<ItemProperty> properties;
};

} // namespace nw
