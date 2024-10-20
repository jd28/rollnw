#pragma once

#include "../serialization/Serialization.hpp"
#include "../util/FixedVector.hpp"
#include "../util/Variant.hpp"
#include "ObjectBase.hpp"

#include <cstdint>

namespace nw {

struct Item;

struct InventoryItem {
    bool infinite = false;
    uint16_t pos_x;
    uint16_t pos_y;
    Variant<Resref, Item*> item;
};

struct Inventory {
    explicit Inventory(nw::MemoryResource* allocator = nw::kernel::global_allocator());
    explicit Inventory(ObjectBase* owner_, nw::MemoryResource* allocator = nw::kernel::global_allocator());

    Inventory(const Inventory&) = delete;
    Inventory(Inventory&&) = default;
    Inventory& operator=(const Inventory&) = delete;
    Inventory& operator=(Inventory&&) = default;
    ~Inventory() = default;

    void destroy();
    bool instantiate();

    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    nlohmann::json to_json(SerializationProfile profile) const;

    ObjectBase* owner;
    FixedVector<InventoryItem> items;
};

bool deserialize(Inventory& self, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Inventory& self, GffBuilderStruct& archive, SerializationProfile profile);

} // namespace nw
