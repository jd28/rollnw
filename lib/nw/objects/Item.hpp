#pragma once

#include "../serialization/Serialization.hpp"
#include "ObjectBase.hpp"
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
    uint16_t type;
    uint16_t subtype;
    uint8_t cost_table;
    uint16_t cost_value;
    uint8_t param_table;
    uint8_t param_value;
};

struct Item {
    Item() = default;
    Item(const GffInputArchiveStruct gff, SerializationProfile profile);
    ~Item() = default;

    Resref resref;
    std::string tag;
    LocString name;
    LocString description;
    LocString description_id;
    Inventory inventory;

    uint32_t cost = 0;
    uint32_t additional_cost = 0;
    int32_t baseitem;
    uint16_t stacksize = 1;
    uint8_t charges = 0;
    uint8_t cursed = 0;
    uint8_t plot = 0;
    uint8_t stolen = 0;

    ItemModelType model_type = ItemModelType::simple;
    std::array<uint8_t, 6> model_colors;
    std::array<uint8_t, 19> model_parts;
    std::vector<ItemProperty> properties;

    // Blueprint only
    std::string comment;
    uint8_t palette_id = ~0;

    // Instance only
    Location loc;

    LocalData local_data;
};

} // namespace nw
