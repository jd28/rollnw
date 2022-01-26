#include "Inventory.hpp"

#include "../Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Inventory::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    size_t sz = archive["ItemList"].size();
    items.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        auto st = archive["ItemList"][i];
        InventoryItem ii;

        if (owner->common()->object_type == ObjectType::store) {
            st.get_to("Infinite", ii.infinite, false);
        }
        st.get_to("Repos_PosX", ii.pos_x);
        st.get_to("Repos_Posy", ii.pos_y); // Not typo..

        if (SerializationProfile::blueprint == profile) {
            if (auto r = st.get<Resref>("InventoryRes")) {
                ii.item = *r;
            }
        } else if (SerializationProfile::instance == profile) {
            ii.item = std::make_unique<Item>(st, profile);
        }

        items.push_back(std::move(ii));
    }

    return true;
}

bool Inventory::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    if (!archive.is_array()) { return false; }

    try {
        items.reserve(archive.size());
        for (size_t i = 0; i < archive.size(); ++i) {
            InventoryItem ii;
            if (owner->common()->object_type == ObjectType::store) {
                archive[i].at("infinite").get_to(ii.infinite);
            }
            archive[i].at("position")[0].get_to(ii.pos_x);
            archive[i].at("position")[1].get_to(ii.pos_y);
            if (profile == SerializationProfile::blueprint) {
                ii.item = archive[i].at("item").get<Resref>();
            } else {
                ii.item = std::make_unique<Item>(archive[i].at("item"), profile);
            }
            items.push_back(std::move(ii));
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Inventory::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Inventory::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    if (items.empty()) { return true; }
    auto& list = archive.add_list("ItemList");
    for (const auto& it : items) {
        auto& str = list.push_back(static_cast<uint32_t>(list.size()))
                        .add_field("Repos_PosX", it.pos_x)
                        .add_field("Repos_Posy", it.pos_y);

        if (owner->common()->object_type == ObjectType::store && it.infinite) {
            str.add_field("Infinite", it.infinite);
        }

        if (SerializationProfile::blueprint == profile) {
            str.add_field("InventoryRes", std::get<Resref>(it.item));
        } else {
            std::get<std::unique_ptr<Item>>(it.item)->to_gff(str, profile);
        }
    }
    return true;
}

nlohmann::json Inventory::to_json(SerializationProfile profile) const
{
    nlohmann::json j = nlohmann::json::array();

    try {
        for (const auto& it : items) {
            j.push_back({});
            auto& payload = j.back();
            if (owner->common()->object_type == ObjectType::store) {
                payload["infinite"] = it.infinite;
            }

            payload["position"] = {it.pos_x, it.pos_y};
            if (std::holds_alternative<std::unique_ptr<Item>>(it.item)) {
                payload["item"] = std::get<std::unique_ptr<Item>>(it.item)->to_json(profile);
            } else {
                if (SerializationProfile::blueprint == profile) {
                    payload["item"] = std::get<Resref>(it.item);
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "inventory::to_json exception: {}", e.what());
        j.clear();
    }

    return j;
}

} // namespace nw
