#pragma once

#include "Common.hpp"
#include "Inventory.hpp"
#include "Item.hpp"
#include "ObjectBase.hpp"

#include <vector>

namespace nw {

struct StoreScripts {
    Resref on_closed;
    Resref on_opened;
};

/// Store Inventory component
struct StoreInventory {
    StoreInventory() = default;
    StoreInventory(ObjectBase* owner);

    /// Sets inventory owner
    void set_owner(ObjectBase* owner);

    Inventory armor;
    Inventory miscellaneous;
    Inventory potions;
    Inventory rings;
    Inventory weapons;
    std::vector<int32_t> will_not_buy;
    std::vector<int32_t> will_only_buy;
};

struct Store : public ObjectBase {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::store;
    static constexpr ResourceType::type restype = ResourceType::utm;

    Store();

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    Store* as_store() override { return this; }
    const Store* as_store() const override { return this; }
    virtual bool instantiate() override;

    static bool deserialize(Store* obj, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const Store* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
    StoreScripts scripts;
    StoreInventory inventory;

    int32_t blackmarket_markdown = 0;
    int32_t identify_price = 100;
    int32_t markdown = 0;
    int32_t markup = 0;
    int32_t max_price = -1;
    int32_t gold = -1;

    bool blackmarket;

    bool instantiated_ = false;
};

// == Store - Serialization - Gff =============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Store* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Store* obj, SerializationProfile profile);
bool serialize(const Store* obj, GffBuilderStruct& archive, SerializationProfile profile);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
