#pragma once

#include "Item.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"

#include <vector>

namespace nw {

struct StoreScripts {
    Resref on_closed;
    Resref on_opened;
};

struct Store : public ObjectBase {
    Store();
    Store(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Store(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::store;

    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual bool instantiate() override;
    virtual Store* as_store() override { return this; }
    virtual const Store* as_store() const override { return this; }

    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    GffOutputArchive to_gff(SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    Inventory armor;
    Inventory miscellaneous;
    Inventory potions;
    Inventory rings;
    Inventory weapons;
    StoreScripts scripts;
    std::vector<int32_t> will_not_buy;
    std::vector<int32_t> will_only_buy;

    int32_t blackmarket_markdown = 0;
    int32_t identify_price = 100;
    int32_t markdown = 0;
    int32_t markup = 0;
    int32_t max_price = -1;
    int32_t gold = -1;

    bool blackmarket;

private:
    bool valid_ = true;
};

} // namespace nw
