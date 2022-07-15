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
    StoreInventory(flecs::entity owner);

    /// Sets inventory owner
    void set_owner(flecs::entity owner);

    Inventory armor;
    Inventory miscellaneous;
    Inventory potions;
    Inventory rings;
    Inventory weapons;
    std::vector<int32_t> will_not_buy;
    std::vector<int32_t> will_only_buy;
};

struct Store {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::store;

    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const flecs::entity ent, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile);

    int32_t blackmarket_markdown = 0;
    int32_t identify_price = 100;
    int32_t markdown = 0;
    int32_t markup = 0;
    int32_t max_price = -1;
    int32_t gold = -1;

    bool blackmarket;
};

} // namespace nw
