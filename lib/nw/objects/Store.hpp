#pragma once

#include "Item.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"

#include <vector>

namespace nw {

struct Store : public ObjectBase {
    Store();
    Store(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Store(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;

    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Store* as_store() override { return this; }
    virtual const Store* as_store() const override { return this; }

    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    bool valid_ = true;

    Common common_;
    Resref on_closed;
    Resref on_opened;

    int32_t blackmarket_markdown = 0;
    int32_t identify_price = 100;
    int32_t markdown = 0;
    int32_t markup = 0;
    int32_t max_price = -1;
    int32_t gold = -1;

    std::vector<int32_t> will_not_buy;
    std::vector<int32_t> will_only_buy;

    bool blackmarket;

    Inventory armor;
    Inventory miscellaneous;
    Inventory potions;
    Inventory rings;
    Inventory weapons;
};

} // namespace nw
